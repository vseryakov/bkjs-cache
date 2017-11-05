# Cache module for node and backendjs

# Usage

 - Generic cache outside of V8 heap, implements named sets of caches, `name` refers to a separate named cache
   with its own set of keys:
   - `put(name, key, value)` - save a key value pair in the named cache
   - `incr(name, key, value)` - increment a key value, non existent keys are assumed to be 0
   - `get(name, key)` - return value for a key
   - `del(name, key)` - delete a key
   - `exists(name, key)` - returns true if a key exists
   - `keys(name)` - return a list of all keys in the named cache
   - `clear(name, ttl)` - delete all keys, if ttl is greater than 0 then it must be a number of seconds the cache will be alive till it expires,
      all subsequent items put in the cache will live until it expires automatically
   - `names()` - returs all existing named caches
   - `size(name)` - returns size of a cache
   - `each(name, callback)` - call a callback for each key
   - `begin(name)` - returns first key
   - `next(name)` - returns next key or undefined when reached the end
 - LRU internal cache outside of V8 heap:
   - `lruInit(max)` - init LRU cache with max number of keys, this is in-memory cache which evicts older keys
   - `lruStats()` - return statistics about the LRU cache
   - `lruSize()` - return size of the current LRU cache
   - `lruCount()` - number of keys in the LRU cache
   - `lruPut(name, val [, expire])` - set/replace value by name, expire is the time in the future when this key becomes invalid
   - `lruGet(name [, now])` - return value by name, if `now` is given in ms then a key with ttl below this timestamp will be considered
     invalid and deleted even if the expiration never been set before
   - `lruIncr(name, val [, expire])` - increase value by given number, non existent items assumed to be 0
   - `lruDel(name)` - delete by name
   - `lruKeys([pattern] [,level])` - return all cache key names, if pattern is given match all keys beginning with it, if details is 1 then return
     an array with keys and expiration, if details is 2 then return an array with item key, value and expiration
   - `lruClear()` - clear LRU cache
   - `lruClean()` - remove the oldest item from the cache
   - `lruFront()` - return the oldest key

# Author

Vlad Seryakov

