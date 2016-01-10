# Cache module for node and backendjs

# Usage

 - Generic cache outside of V8 heap, implements named sets of caches, `name` refers to a separate named cache
   with its own set of keys:
   - `save(name, file, separator)` - dump names cache contents into a file
   - `put(name, key, value)` - save a key value pair in the named cache
   - `incr(name, key, value)` - increment a key value, non existent keys are assumed to be 0
   - `get(name, key)` - return value for a key
   - `del(name, key)` - delete a key
   - `exists(name, key)` - returns true if a key exists
   - `keys(name)` - return a list of all keys in the named cache
   - `clear(name)` - delete all keys
   - `names()` - returs all existing named caches
   - `size(name)` - returns size of a cache
   - `each(name, callback)` - call a callback for each key
   - `forEach(name, onitem, oncomplete)`
   - `forEachNext(name)` - to be called inside a callback passed to ForEach call to move to the next key
   - `begin(name)` - returns first key
   - `next(name)` - returns next key or undefined when reached the end
 - LRU internal cache outside of V8 heap:
   - `lruInit(max)` - init LRU cache with max number of keys, this is in-memory cache which evicts older keys
   - `lruStats()` - return statistics about the LRU cache
   - `lruSize()` - return size of the current LRU cache
   - `lruCount()` - number of keys in the LRU cache
   - `lruPut(name, val [, expire])` - set/replace value by name, expire is the time in the future when this key becomes invalid
   - `lruGet(name [, now])` - return value by name, if now is given all keys below this timestamp will be consideed invalid and deleted
   - `lruIncr(name, val [, expire])` - increase value by given number, non existent items assumed to be 0
   - `lruDel(name)` - delete by name
   - `lruKeys()` - return all cache key names
   - `lruClear()` - clear LRU cache

# Author

Vlad Seryakov

