//
//  Author: Vlad Seryakov vseryakov@gmail.com
//  April 2013
//

#include <node.h>
#include <node_object_wrap.h>
#include <node_buffer.h>
#include <node_version.h>
#include <v8.h>
#include <v8-profiler.h>
#include <uv.h>
#include <nan.h>

using namespace node;
using namespace v8;

#include <algorithm>
#include <vector>
#include <string>
#include <set>
#include <list>
#include <map>
#include <queue>
#if defined __APPLE__ && __cplusplus < 201103L
#include <tr1/unordered_map>
using namespace std::tr1;
#else
#include <unordered_map>
#endif
using namespace std;

#define NAN_REQUIRE_ARGUMENT_STRING(i, var) if (info.Length() <= (i) || !info[i]->IsString()) {Nan::ThrowError("Argument " #i " must be a string"); return;} Nan::Utf8String var(info[i]->ToString());
#define NAN_REQUIRE_ARGUMENT_AS_STRING(i, var) if (info.Length() <= (i)) {Nan::ThrowError("Argument " #i " must be a string"); return;} Nan::Utf8String var(info[i]->ToString());
#define NAN_REQUIRE_ARGUMENT_INT(i, var) if (info.Length() <= (i)) {Nan::ThrowError("Argument " #i " must be an integer"); return;} int var = info[i]->Int32Value();
#define NAN_REQUIRE_ARGUMENT_FUNCTION(i, var) if (info.Length() <= (i) || !info[i]->IsFunction()) {Nan::ThrowError("Argument " #i " must be a function"); return;} Local<Function> var = Local<Function>::Cast(info[i]);
#define NAN_OPTIONAL_ARGUMENT_STRING(i, var) Nan::Utf8String var(info.Length() > (i) && info[i]->IsString() ? info[i]->ToString() : Nan::EmptyString());
#define NAN_OPTIONAL_ARGUMENT_AS_INT(i, var, dflt) int var = (info.Length() > (i) ? info[i]->Int32Value() : dflt);
#define NAN_OPTIONAL_ARGUMENT_AS_UINT64(i, var, dflt) uint64_t var = (info.Length() > (i) ? info[i]->NumberValue() : dflt);
#define NAN_OPTIONAL_ARGUMENT_AS_STRING(i, var) Nan::Utf8String var(info.Length() > (i) ? info[i]->ToString() : Nan::EmptyString());
#define NAN_TRY_CATCH_CALL(context, callback, argc, argv) { Nan::TryCatch try_catch; (callback)->Call((context), (argc), (argv)); if (try_catch.HasCaught()) FatalException(try_catch); }

static const string empty;

typedef map<string, string> bkStringMap;

struct LRUStringCache {
    typedef unordered_map<string, pair<pair<string,uint64_t>,list<string>::iterator> > LRUStringItems;
    size_t size;
    size_t max;
    list<string> lru;
    LRUStringItems items;
    // stats
    size_t hits, misses, cleans, ins, dels;

    LRUStringCache(int m = 100000): max(m) { clear(); }
    ~LRUStringCache() { clear(); }

    const string& get(const string& k, uint64_t now) {
        const LRUStringItems::iterator it = items.find(k);
        if (it == items.end()) {
            misses++;
            return empty;
        }
        hits++;
        if (now > 0 && now > it->second.first.second) {
            del(k);
            return empty;
        }
        lru.splice(lru.end(), lru, it->second.second);
        return it->second.first.first;
    }
    const string& put(const string& k, const string& v, uint64_t expire) {
        if (items.size() >= max) clean();
        const LRUStringItems::iterator it = items.find(k);
        if (it == items.end()) {
            list<string>::iterator it = lru.insert(lru.end(), k);
            pair<LRUStringItems::iterator,bool> p = items.insert(std::make_pair(k, std::make_pair(std::make_pair(v, expire), it)));
            Nan::AdjustExternalMemory(k.size() + v.size());
            size += k.size() + v.size();
            ins++;
            return p.first->second.first.first;
        } else {
            Nan::AdjustExternalMemory(-it->second.first.first.size());
            it->second.first.first = v;
            it->second.first.second = expire;
            Nan::AdjustExternalMemory(v.size());
            lru.splice(lru.end(), lru, it->second.second);
            return it->second.first.first;
        }
    }
    bool exists(const string &k) {
        const LRUStringItems::iterator it = items.find(k);
        return it != items.end();
    }
    const string &incr(const string& k, const string& v, int64_t expire) {
        const string& o = get(k, 0);
        char val[32];
        sprintf(val, "%lld", atoll(o.c_str()) + atoll(v.c_str()));
        return put(k, val, expire);
    }
    void del(const string &k) {
        const LRUStringItems::iterator it = items.find(k);
        if (it == items.end()) return;
        size -= k.size() + it->second.first.first.size();
        Nan::AdjustExternalMemory(-(k.size() + it->second.first.first.size()));
        lru.erase(it->second.second);
        items.erase(it);
        dels++;
    }
    string front() {
        return lru.size() ? lru.front() : empty;
    }
    void clean() {
        const LRUStringItems::iterator it = items.find(lru.front());
        if (it == items.end()) return;
        size -= it->first.size() + it->second.first.first.size();
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
    uint64_t expire;

    StringCache(): expire(0) {
        nextIt = items.end();
    }
    ~StringCache() { clear(0); }
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
    void clear(int ttl) {
        bkStringMap::iterator it;
        int n = 0;
        for (it = items.begin(); it != items.end(); ++it) {
            n += it->first.size() + it->second.size();
        }
        items.clear();
        nextIt = items.end();
        Nan::AdjustExternalMemory(-n);
        expire = ttl > 0 ? time(0) + ttl : 0;
    }

    bool begin() {
        nextIt = items.begin();
        return true;
    }
    Local<Value> next() {
        Nan::EscapableHandleScope scope;
        if (nextIt == items.end()) return scope.Escape(Nan::Undefined());
        Local<Array> obj = Nan::New<Array>();
        obj->Set(Nan::New(0), Nan::New(nextIt->first).ToLocalChecked());
        obj->Set(Nan::New(1), Nan::New(nextIt->second).ToLocalChecked());
        nextIt++;
        return scope.Escape(obj);
    }
    void each(Local<Function> cb) {
        bkStringMap::const_iterator it = items.begin();
        while (it != items.end()) {
            Nan::HandleScope scope;
            Local<Value> argv[2];
            argv[0] = Nan::New(it->first).ToLocalChecked();
            argv[1] = Nan::New(it->second).ToLocalChecked();
            NAN_TRY_CATCH_CALL(Nan::GetCurrentContext()->Global(), cb, 2, argv);
            it++;
        }
    }
};

typedef map<std::string, ::StringCache> Cache;

static Cache _cache;
static LRUStringCache _lru;

NAN_METHOD(clear)
{
    NAN_REQUIRE_ARGUMENT_AS_STRING(0, name);
    NAN_OPTIONAL_ARGUMENT_AS_INT(1, ttl, 0);

    Cache::iterator itc = _cache.find(*name);
    if (itc != _cache.end()) {
        itc->second.clear(ttl);
        _cache.erase(*name);
    } else
    if (ttl > 0) {
        _cache[*name] = StringCache();
        itc = _cache.find(*name);
        itc->second.clear(ttl);
    }
}

NAN_METHOD(put)
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

NAN_METHOD(incr)
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
    info.GetReturnValue().Set(Nan::New(v).ToLocalChecked());
}

NAN_METHOD(del)
{
    NAN_REQUIRE_ARGUMENT_AS_STRING(0, name);
    NAN_REQUIRE_ARGUMENT_AS_STRING(1, key);

    Cache::iterator itc = _cache.find(*name);
    if (itc != _cache.end()) itc->second.del(*key);
}

NAN_METHOD(get)
{
    NAN_REQUIRE_ARGUMENT_AS_STRING(0, name);
    NAN_REQUIRE_ARGUMENT_AS_STRING(1, key);

    Cache::iterator itc = _cache.find(*name);
    if (itc != _cache.end()) {
        info.GetReturnValue().Set(Nan::New(itc->second.get(*key)).ToLocalChecked());
        return;
    }
}

NAN_METHOD(exists)
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

NAN_METHOD(keys)
{
    NAN_REQUIRE_ARGUMENT_AS_STRING(0, name);

    Local<Array> keys = Nan::New<Array>();
    Cache::iterator itc = _cache.find(*name);
    if (itc != _cache.end()) {
        bkStringMap::const_iterator it = itc->second.items.begin();
        int i = 0;
        while (it != itc->second.items.end()) {
            keys->Set(Nan::New(i), Nan::New(it->first).ToLocalChecked());
            it++;
            i++;
        }
    }
    info.GetReturnValue().Set(keys);
}

NAN_METHOD(names)
{
    Local<Array> keys = Nan::New<Array>();
    Cache::const_iterator it = _cache.begin();
    int i = 0;
    while (it != _cache.end()) {
        Local<String> str = Nan::New(it->first).ToLocalChecked();
        keys->Set(Nan::New(i), str);
        it++;
        i++;
    }
    info.GetReturnValue().Set(keys);
}

NAN_METHOD(size)
{
    NAN_REQUIRE_ARGUMENT_AS_STRING(0, name);
    int count = 0;
    Cache::iterator itc = _cache.find(*name);
    if (itc != _cache.end()) count = itc->second.items.size();
    info.GetReturnValue().Set(Nan::New(count));
}

NAN_METHOD(each)
{
    NAN_REQUIRE_ARGUMENT_AS_STRING(0, name);
    NAN_REQUIRE_ARGUMENT_FUNCTION(1, cb);

    Cache::iterator itc = _cache.find(*name);
    if (itc != _cache.end()) itc->second.each(cb);
}

NAN_METHOD(begin)
{
    NAN_REQUIRE_ARGUMENT_AS_STRING(0, name);
    Cache::iterator itc = _cache.find(*name);
    if (itc != _cache.end()) {
        info.GetReturnValue().Set(Nan::New(itc->second.begin()));
    } else {
        info.GetReturnValue().Set(Nan::False());
    }
}

NAN_METHOD(next)
{
    NAN_REQUIRE_ARGUMENT_AS_STRING(0, name);
    Cache::iterator itc = _cache.find(*name);
    if (itc != _cache.end()) info.GetReturnValue().Set(itc->second.next());
}

NAN_METHOD(lruInit)
{
    NAN_REQUIRE_ARGUMENT_INT(0, max);
    if (max > 0) _lru.max = max;
}

NAN_METHOD(lruSize)
{
    info.GetReturnValue().Set(Nan::New((double)_lru.size));
}

NAN_METHOD(lruCount)
{
    info.GetReturnValue().Set(Nan::New((double)_lru.items.size()));
}

NAN_METHOD(lruClear)
{
    _lru.clear();
}

NAN_METHOD(lruClean)
{
    _lru.clean();
}

NAN_METHOD(lruPut)
{
    NAN_REQUIRE_ARGUMENT_AS_STRING(0, key);
    NAN_REQUIRE_ARGUMENT_AS_STRING(1, val);
    NAN_OPTIONAL_ARGUMENT_AS_UINT64(2, expire, 0);

    _lru.put(*key, *val, expire);
}

NAN_METHOD(lruIncr)
{
    NAN_REQUIRE_ARGUMENT_AS_STRING(0, key);
    NAN_REQUIRE_ARGUMENT_AS_STRING(1, val);
    NAN_OPTIONAL_ARGUMENT_AS_UINT64(2, expire, 0);

    const string& str = _lru.incr(*key, *val, expire);
    info.GetReturnValue().Set(Nan::New(str).ToLocalChecked());
}

NAN_METHOD(lruGet)
{
    NAN_REQUIRE_ARGUMENT_AS_STRING(0, key);
    NAN_OPTIONAL_ARGUMENT_AS_UINT64(1, now, 0);
    const string& str = _lru.get(*key, now);
    info.GetReturnValue().Set(Nan::New(str).ToLocalChecked());
}

NAN_METHOD(lruFront)
{
    const string& str = _lru.front();
    info.GetReturnValue().Set(Nan::New(str).ToLocalChecked());
}

NAN_METHOD(lruDel)
{
    NAN_REQUIRE_ARGUMENT_AS_STRING(0, key);
    _lru.del(*key);
}

NAN_METHOD(lruExists)
{
    NAN_REQUIRE_ARGUMENT_AS_STRING(0, key);
    info.GetReturnValue().Set(Nan::New(_lru.exists(*key)));
}

NAN_METHOD(lruKeys)
{
    NAN_OPTIONAL_ARGUMENT_AS_STRING(0, str);
    NAN_OPTIONAL_ARGUMENT_AS_INT(1, details, 0);
    Local<Array> keys = Nan::New<Array>();
    char *key = *str;
    int i = 0, n = strlen(key);
    if (!details) {
        list<string>::iterator it = _lru.lru.begin();
        while (it != _lru.lru.end()) {
            if (!*key || !strncmp(it->c_str(), key, n)) {
                keys->Set(Nan::New(i), Nan::New(it->c_str()).ToLocalChecked());
                i++;
            }
            it++;
        }
    } else {
        LRUStringCache::LRUStringItems::iterator it = _lru.items.begin();
        while (it != _lru.items.end()) {
            if (!*key || !strncmp(it->first.c_str(), key, n)) {
                switch (details) {
                case 1: {
                    Local<Object> obj = Nan::New<Object>();
                    obj->Set(Nan::New("key").ToLocalChecked(), Nan::New(it->first).ToLocalChecked());
                    obj->Set(Nan::New("expire").ToLocalChecked(), Nan::New((double)it->second.first.second));
                    keys->Set(Nan::New(i), obj);
                    break;
                }
                default: {
                    Local<Object> obj = Nan::New<Object>();
                    obj->Set(Nan::New("key").ToLocalChecked(), Nan::New(it->first).ToLocalChecked());
                    obj->Set(Nan::New("expire").ToLocalChecked(), Nan::New((double)it->second.first.second));
                    obj->Set(Nan::New("value").ToLocalChecked(), Nan::New(it->second.first.first).ToLocalChecked());
                    keys->Set(Nan::New(i), obj);
                }}
                i++;
            }
            it++;
        }
    }
    info.GetReturnValue().Set(keys);
}

static void ClearCacheTimer(uv_timer_t *req)
{
    uint64_t now = time(0);
    Cache::iterator itc = _cache.begin();
    while (itc != _cache.end()) {
        if (itc->second.expire  > 0 && itc->second.expire < now) itc->second.clear(0);
        itc++;
    }
}

NAN_METHOD(lruStats)
{
    Local<Object> obj = Nan::New<Object>();
    obj->Set(Nan::New("inserted").ToLocalChecked(), Nan::New((double)_lru.ins));
    obj->Set(Nan::New("deleted").ToLocalChecked(), Nan::New((double)_lru.dels));
    obj->Set(Nan::New("cleanups").ToLocalChecked(), Nan::New((double)_lru.cleans));
    obj->Set(Nan::New("hits").ToLocalChecked(), Nan::New((double)_lru.hits));
    obj->Set(Nan::New("misses").ToLocalChecked(), Nan::New((double)_lru.misses));
    obj->Set(Nan::New("max").ToLocalChecked(), Nan::New((double)_lru.max));
    obj->Set(Nan::New("size").ToLocalChecked(), Nan::New((double)_lru.size));
    obj->Set(Nan::New("count").ToLocalChecked(), Nan::New((double)_lru.items.size()));
    info.GetReturnValue().Set(obj);
}

static NAN_MODULE_INIT(CacheInit) {
    Nan::HandleScope scope;

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
    NAN_EXPORT(target, lruClean);
    NAN_EXPORT(target, lruFront);

    uv_timer_t *req = new uv_timer_t;
    uv_timer_init(uv_default_loop(), req);
    uv_timer_start(req, (uv_timer_cb)ClearCacheTimer, 0, 60000);
}

NODE_MODULE(binding, CacheInit);
