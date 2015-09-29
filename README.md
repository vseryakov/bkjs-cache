# Cache module for node and backendjs

# Usage

 - Generic cache outside of V8 heap, `name` refers to a separate named cache with its own set of keys
   - `cacheSave(name, file, separator)` - dump names cache contents into a file
   - `cachePut(name, key, value)` - save a key value pair in the named cache
   - `cacheIncr(name, key, value)` - increment a key value, non existent keys are assumed to be 0
   - `cacheGet(name, key)` - return value for a key
   - `cacheDel(name, key)` - delete a key 
   - `cacheExists(name, key)` - returns true if a key exists
   - `cacheKeys(name)` - return a list of all keys in the named cache
   - `cacheClear(name)` - delete all keys
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

