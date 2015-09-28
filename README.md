# Cache module for node and backendjs

# Usage

 - Generic cache outside of V8 heap, `name` refers to a separate named cache with its own set of keys
   - `cacheSave(name, file, separator)` - dump names cache contents into a file
   - `cachePut(name, key, value)`
   - `cacheIncr(name, key, value)`
   - `cacheGet(name, key)`
   - `cacheDel(name, key)`
   - `cacheExists(name, key)`
   - `cacheKeys(name)`
   - `cacheClear(name)`
   - `cacheNames()` - returs all existing named caches
   - `cacheSize(name)` - returns size of a cache
   - `cacheEach(name, callback)` - call a callback for each key
   - `cacheForEach(name, onitem, oncomplete)`
   - `cacheForEachNext(name)` - to be called inside a callback passed to ForEach call to move to the next key
   - `cacheBegin(name)` - returns first key 
   - `cacheNext(name)` - returns next key or undefined when reached the end
 - LRU internal cache outside of V8 heap
   - `lruInit(max)` - init LRU cache with max number of keys, this is in-memory cache which evicts older keys
   - `lruStats()` - return statistics about the LRU cache
   - `lruSize()` - return size of the current LRU cache
   - `lruCount()` - number of keys in the LRU cache
   - `lruPut(name, val)` - set/replace value by name
   - `lruGet(name)` - return value by name
   - `lruIncr(name, val)` - increase value by given number, non existent items assumed to be 0
   - `lruDel(name)` - delete by name
   - `lruKeys()` - return all cache key names
   - `lruClear()` - clear LRU cache

# Author 

Vlad Seryakov

