//
//  Author: Vlad Seryakov vseryakov@gmail.com
//  April 2013
//

#include "bkjs.h"
#include "bklib.h"

static const string empty;

struct LRUStringCache {
    typedef unordered_map<string, pair<string, list<string>::iterator> > LRUStringItems;
    size_t size;
    size_t max;
    list<string> lru;
    LRUStringItems items;
    // stats
    size_t hits, misses, cleans, ins, dels;

    LRUStringCache(int m = 100000): max(m) { clear(); }
    ~LRUStringCache() { clear(); }

    const string& get(const string& k) {
        const LRUStringItems::iterator it = items.find(k);
        if (it == items.end()) {
            misses++;
            return empty;
        }
        hits++;
        lru.splice(lru.end(), lru, it->second.second);
        return it->second.first;
    }
    const string& put(const string& k, const string& v) {
        if (items.size() >= max) clean();
        const LRUStringItems::iterator it = items.find(k);
        if (it == items.end()) {
            list<string>::iterator it = lru.insert(lru.end(), k);
            pair<LRUStringItems::iterator,bool> p = items.insert(std::make_pair(k, std::make_pair(v, it)));
            Nan::AdjustExternalMemory(k.size() + v.size());
            size += k.size() + v.size();
            ins++;
            return p.first->second.first;
        } else {
            Nan::AdjustExternalMemory(-it->second.first.size());
            it->second.first = v;
            Nan::AdjustExternalMemory(v.size());
            lru.splice(lru.end(), lru, it->second.second);
            return it->second.first;
        }
    }
    bool exists(const string &k) {
        const LRUStringItems::iterator it = items.find(k);
        return it != items.end();
    }
    const string &incr(const string& k, const string& v) {
        const string& o = get(k);
        char val[32];
        sprintf(val, "%lld", atoll(o.c_str()) + atoll(v.c_str()));
        return put(k, val);
    }
    void del(const string &k) {
        const LRUStringItems::iterator it = items.find(k);
        if (it == items.end()) return;
        size -= k.size() + it->second.first.size();
        Nan::AdjustExternalMemory(-(k.size() + it->second.first.size()));
        lru.erase(it->second.second);
        items.erase(it);
        dels++;
    }
    void clean() {
        const LRUStringItems::iterator it = items.find(lru.front());
        if (it == items.end()) return;
        size -= it->first.size() + it->first.size();
        items.erase(it);
        lru.pop_front();
        cleans++;
    }
    void clear() {
        items.clear();
        lru.clear();
        Nan::AdjustExternalMemory(-size);
        size = ins = dels = cleans = hits = misses = 0;
    }
};

struct StringCache {
    bkStringMap items;
    bkStringMap::const_iterator nextIt;
    Persistent<Function> nextCb;
    Persistent<Function> completed;

    StringCache() { nextIt = items.end(); }
    ~StringCache() { clear(); }
    const string &get(const string &key) {
        bkStringMap::iterator it = items.find(key);
        if (it != items.end()) return it->second;
        return empty;
    }
    string &put(const string &key, const string &val) {
        bkStringMap::iterator it = items.find(key);
        if (it != items.end()) {
            Nan::AdjustExternalMemory(-it->second.size());
            it->second = val;
            Nan::AdjustExternalMemory(val.size());
            return it->second;
        } else {
            it = items.insert(items.end(), std::pair<string,string>(key, val));
            Nan::AdjustExternalMemory(key.size() + val.size());
            return it->second;
        }
    }
    bool exists(const string &k) {
        bkStringMap::iterator it = items.find(k);
        return it != items.end();
    }
    const string &incr(const string& k, const string& v) {
        string o = get(k);
        char val[32];
        sprintf(val, "%lld", atoll(o.c_str()) + atoll(v.c_str()));
        return put(k, val);
    }
    void del(const string &key) {
        bkStringMap::iterator it = items.find(key);
        if (it != items.end()) {
            Nan::AdjustExternalMemory(-(it->first.size() + it->second.size()));
            items.erase(it);
        }
    }
    void clear() {
        bkStringMap::iterator it;
        int n = 0;
        for (it = items.begin(); it != items.end(); ++it) {
            n += it->first.size() + it->second.size();
        }
        items.clear();
        nextIt = items.end();
        Nan::AdjustExternalMemory(-n);
        if (!nextCb.IsEmpty()) nextCb.Dispose();
        if (!completed.IsEmpty()) completed.Dispose();
        nextCb.Clear();
        completed.Clear();
    }

    bool begin(Handle<Function> cb1 = Handle<Function>(), Handle<Function> cb2 = Handle<Function>()) {
        if (!nextCb.IsEmpty()) nextCb.Dispose();
        if (!completed.IsEmpty()) completed.Dispose();
        nextCb = Persistent<Function>::New(cb1);
        completed = Persistent<Function>::New(cb2);
        nextIt = items.begin();
        return true;
    }
    Handle<Value> next() {
        Nan::EscapableHandleScope scope;
        if (nextIt == items.end()) return scope.Escape(Nan::Undefined());
        Local<Array> obj(Array::New());
        obj->Set(Nan::New(0), Nan::New(nextIt->first.c_str()).ToLocalChecked());
        obj->Set(Nan::New(1), Nan::New(nextIt->second.c_str()).ToLocalChecked());
        nextIt++;
        return scope.Escape(obj);
    }
    void each(Handle<Function> cb) {
        bkStringMap::const_iterator it = items.begin();
        while (it != items.end()) {
            Nan::HandleScope scope;
            Local<Value> argv[2];
            argv[0] = Nan::New(it->first.c_str()).ToLocalChecked();
            argv[1] = Nan::New(it->second.c_str()).ToLocalChecked();
            NAN_TRY_CATCH_CALL(Context::GetCurrent()->Global(), cb, 2, argv);
            it++;
        }
    }
    bool timer() {
        if (nextIt != items.end()) {
            uv_work_t *req = new uv_work_t;
            req->data = this;
            uv_queue_work(uv_default_loop(), req, WorkTimer, (uv_after_work_cb)AfterTimer);
            return true;
        } else {
            uv_timer_t *req = new uv_timer_t;
            uv_timer_init(uv_default_loop(), req);
            req->data = this;
            uv_timer_start(req, CompletedTimer, 0, 0);
            return false;
        }
    }
    static void WorkTimer(uv_work_t *req) {}
    static void AfterTimer(uv_work_t *req, int status) {
        Nan::HandleScope scope;
        StringCache *cache = (StringCache *)req->data;
        Local<Value> argv[2];
        if (cache->nextIt != cache->items.end()) {
            argv[0] = Nan::New(cache->nextIt->first.c_str()).ToLocalChecked();
            argv[1] = Nan::New(cache->nextIt->second.c_str()).ToLocalChecked();
            cache->nextIt++;
            NAN_TRY_CATCH_CALL(Context::GetCurrent()->Global(), cache->nextCb, 2, argv);
        }
        delete req;
    }
    static void CompletedTimer(uv_timer_t *req, int status) {
        Nan::HandleScope scope;
        StringCache *cache = (StringCache *)req->data;
        Local<Value> argv[1];
        NAN_TRY_CATCH_CALL(Context::GetCurrent()->Global(), cache->completed, 0, argv);
        delete req;
    }
};

typedef map<std::string, ::StringCache> Cache;

static Cache _cache;
static LRUStringCache _lru;

static NAN_METHOD(clear)
{
    NAN_REQUIRE_ARGUMENT_AS_STRING(0, name);

    Cache::iterator itc = _cache.find(*name);
    if (itc != _cache.end()) {
        itc->second.clear();
        _cache.erase(*name);
    }
}

static NAN_METHOD(put)
{
    NAN_REQUIRE_ARGUMENT_AS_STRING(0, name);
    NAN_REQUIRE_ARGUMENT_AS_STRING(1, key);
    NAN_REQUIRE_ARGUMENT_AS_STRING(2, val);

    Cache::iterator itc = _cache.find(*name);
    if (itc == _cache.end()) {
        _cache[*name] = StringCache();
        itc = _cache.find(*name);
    }
    itc->second.put(*key, *val);
}

static NAN_METHOD(incr)
{
    NAN_REQUIRE_ARGUMENT_AS_STRING(0, name);
    NAN_REQUIRE_ARGUMENT_AS_STRING(1, key);
    NAN_REQUIRE_ARGUMENT_AS_STRING(2, val);

    Cache::iterator itc = _cache.find(*name);
    if (itc == _cache.end()) {
        _cache[*name] = StringCache();
        itc = _cache.find(*name);
    }
    string v = itc->second.incr(*key, *val);
    info.GetReturnValue().Set(Nan::New(v.c_str()).ToLocalChecked());
}

static NAN_METHOD(del)
{
    NAN_REQUIRE_ARGUMENT_AS_STRING(0, name);
    NAN_REQUIRE_ARGUMENT_AS_STRING(1, key);

    Cache::iterator itc = _cache.find(*name);
    if (itc != _cache.end()) itc->second.del(*key);
}

static NAN_METHOD(get)
{
    NAN_REQUIRE_ARGUMENT_AS_STRING(0, name);
    NAN_REQUIRE_ARGUMENT_AS_STRING(1, key);

    Cache::iterator itc = _cache.find(*name);
    if (itc != _cache.end()) {
        info.GetReturnValue().Set(Nan::New(itc->second.get(*key).c_str()).ToLocalChecked());
        return;
    }
}

static NAN_METHOD(exists)
{
    NAN_REQUIRE_ARGUMENT_AS_STRING(0, name);
    NAN_REQUIRE_ARGUMENT_AS_STRING(1, key);

    Cache::iterator itc = _cache.find(*name);
    if (itc != _cache.end()) {
        info.GetReturnValue().Set(Nan::New(itc->second.exists(*key)));
    } else {
        info.GetReturnValue().Set(Nan::False());
    }
}

static NAN_METHOD(keys)
{
    NAN_REQUIRE_ARGUMENT_AS_STRING(0, name);

    Local<Array> keys = Array::New();
    Cache::iterator itc = _cache.find(*name);
    if (itc != _cache.end()) {
        bkStringMap::const_iterator it = itc->second.items.begin();
        int i = 0;
        while (it != itc->second.items.end()) {
            keys->Set(Integer::New(i), String::New(it->first.c_str()));
            it++;
            i++;
        }
    }
    info.GetReturnValue().Set(keys);
}

static NAN_METHOD(names)
{
    Local<Array> keys = Array::New();
    Cache::const_iterator it = _cache.begin();
    int i = 0;
    while (it != _cache.end()) {
        Local<String> str = String::New(it->first.c_str());
        keys->Set(Integer::New(i), str);
        it++;
        i++;
    }
    info.GetReturnValue().Set(keys);
}

static NAN_METHOD(size)
{
    NAN_REQUIRE_ARGUMENT_AS_STRING(0, name);
    int count = 0;
    Cache::iterator itc = _cache.find(*name);
    if (itc != _cache.end()) count = itc->second.items.size();
    info.GetReturnValue().Set(Nan::New(count));
}

static NAN_METHOD(each)
{
    NAN_REQUIRE_ARGUMENT_AS_STRING(0, name);
    NAN_REQUIRE_ARGUMENT_FUNCTION(1, cb);

    Cache::iterator itc = _cache.find(*name);
    if (itc != _cache.end()) itc->second.each(cb);
}

static NAN_METHOD(begin)
{
    NAN_REQUIRE_ARGUMENT_AS_STRING(0, name);
    Cache::iterator itc = _cache.find(*name);
    if (itc != _cache.end()) {
        info.GetReturnValue().Set(Nan::New(itc->second.begin()));
    } else {
        info.GetReturnValue().Set(Nan::False());
    }
}

static NAN_METHOD(next)
{
    NAN_REQUIRE_ARGUMENT_AS_STRING(0, name);
    Cache::iterator itc = _cache.find(*name);
    if (itc != _cache.end()) info.GetReturnValue().Set(Nan::New(itc->second.next()));
}

static NAN_METHOD(forEachNext)
{
    NAN_REQUIRE_ARGUMENT_AS_STRING(0, name);
    Cache::iterator itc = _cache.find(*name);
    if (itc != _cache.end()) {
        info.GetReturnValue().Set(Nan::New(itc->second.timer()));
    } else {
        info.GetReturnValue().Set(Nan::False());
    }
}

static NAN_METHOD(forEach)
{
    NAN_REQUIRE_ARGUMENT_AS_STRING(0, name);
    NAN_REQUIRE_ARGUMENT_FUNCTION(1, cb);
    NAN_REQUIRE_ARGUMENT_FUNCTION(2, complete);

    Cache::iterator itc = _cache.find(*name);
    if (itc != _cache.end()) {
        itc->second.begin(cb, complete);
        info.GetReturnValue().Set(Nan::New(itc->second.timer()));
        return;
    }

    Local<Value> argv[1];
    Nan::TryCatch try_catch;
    complete->Call(Context::GetCurrent()->Global(), 0, argv);
    if (try_catch.HasCaught()) FatalException(try_catch);

    info.GetReturnValue().Set(Nan::False());
}

static NAN_METHOD(save)
{
    NAN_REQUIRE_ARGUMENT_AS_STRING(0, name);
    NAN_REQUIRE_ARGUMENT_AS_STRING(1, file);
    NAN_REQUIRE_ARGUMENT_AS_STRING(2, sep);

    FILE *fp = fopen(*file, "w");
    if (!fp) ThrowException(Exception::Error(String::New("Cannot create file")));

    Cache::iterator itc = _cache.find(*name);
    if (itc != _cache.end()) {
        bkStringMap::const_iterator it = itc->second.items.begin();
        while (it != itc->second.items.end()) {
            fprintf(fp, "%s%s%s\n", it->first.c_str(), *sep, it->second.c_str());
            it++;
        }
        fclose(fp);
    }
}

static NAN_METHOD(lruInit)
{
    NAN_REQUIRE_ARGUMENT_INT(0, max);
    if (max > 0) _lru.max = max;
}

static NAN_METHOD(lruSize)
{
    info.GetReturnValue().Set(Nan::New((double)_lru.size));
}

static NAN_METHOD(lruCount)
{
    info.GetReturnValue().Set(Nan::New((double)_lru.items.size()));
}

static NAN_METHOD(lruClear)
{
    _lru.clear();
}

static NAN_METHOD(lruPut)
{
    NAN_REQUIRE_ARGUMENT_AS_STRING(0, key);
    NAN_REQUIRE_ARGUMENT_AS_STRING(1, val);

    _lru.put(*key, *val);
}

static NAN_METHOD(lruIncr)
{
    NAN_REQUIRE_ARGUMENT_AS_STRING(0, key);
    NAN_REQUIRE_ARGUMENT_AS_STRING(1, val);

    const string& str = _lru.incr(*key, *val);
    info.GetReturnValue().Set(Nan::New(str.c_str()).ToLocalChecked());
}

static NAN_METHOD(lruDel)
{
    NAN_REQUIRE_ARGUMENT_AS_STRING(0, key);
    _lru.del(*key);
}

static NAN_METHOD(lruGet)
{
    NAN_REQUIRE_ARGUMENT_AS_STRING(0, key);
    const string& str = _lru.get(*key);
    info.GetReturnValue().Set(Nan::New(str.c_str()).ToLocalChecked());
}

static NAN_METHOD(lruExists)
{
    NAN_REQUIRE_ARGUMENT_AS_STRING(0, key);
    info.GetReturnValue().Set(Nan::New(_lru.exists(*key)));
}

static NAN_METHOD(lruKeys)
{
    NAN_OPTIONAL_ARGUMENT_AS_STRING(0, str);
    Local<Array> keys = Array::New();
    char *key = *str;
    int i = 0, n = strlen(key);
    list<string>::iterator it = _lru.lru.begin();
    while (it != _lru.lru.end()) {
        if (!*key || !strncmp(it->c_str(), key, n)) {
            Local<String> str = String::New(it->c_str());
            keys->Set(Integer::New(i), str);
            i++;
        }
        it++;
    }
    info.GetReturnValue().Set(keys);
}

static NAN_METHOD(lruStats)
{
    Local<Object> obj = Object::New();
    obj->Set(Nan::New("inserted").ToLocalChecked(), Integer::New(_lru.ins));
    obj->Set(Nan::New("deleted").ToLocalChecked(), Integer::New(_lru.dels));
    obj->Set(Nan::New("cleanups").ToLocalChecked(), Integer::New(_lru.cleans));
    obj->Set(Nan::New("hits").ToLocalChecked(), Integer::New(_lru.hits));
    obj->Set(Nan::New("misses").ToLocalChecked(), Integer::New(_lru.misses));
    obj->Set(Nan::New("max").ToLocalChecked(), Integer::New(_lru.max));
    obj->Set(Nan::New("size").ToLocalChecked(), Integer::New(_lru.size));
    obj->Set(Nan::New("count").ToLocalChecked(), Integer::New(_lru.items.size()));
    info.GetReturnValue().Set(obj);
}

void CacheInit(Handle<Object> target)
{
    Nan::HandleScope scope;

    NAN_EXPORT(target, save);
    NAN_EXPORT(target, put);
    NAN_EXPORT(target, incr);
    NAN_EXPORT(target, get);
    NAN_EXPORT(target, exists);
    NAN_EXPORT(target, del);
    NAN_EXPORT(target, keys);
    NAN_EXPORT(target, clear);
    NAN_EXPORT(target, names);
    NAN_EXPORT(target, size);
    NAN_EXPORT(target, each);
    NAN_EXPORT(target, forEach);
    NAN_EXPORT(target, forEachNext);
    NAN_EXPORT(target, begin);
    NAN_EXPORT(target, next);

    NAN_EXPORT(target, lruInit);
    NAN_EXPORT(target, lruStats);
    NAN_EXPORT(target, lruSize);
    NAN_EXPORT(target, lruCount);
    NAN_EXPORT(target, lruPut);
    NAN_EXPORT(target, lruGet);
    NAN_EXPORT(target, lruExists);
    NAN_EXPORT(target, lruIncr);
    NAN_EXPORT(target, lruDel);
    NAN_EXPORT(target, lruKeys);
    NAN_EXPORT(target, lruClear);
}

NODE_MODULE(binding, CacheInit);
