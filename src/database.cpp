#include "../include/Database.h"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <iterator>

// get the instance {singleton}
Database& Database::getInstance() {
    static Database instance;
    return instance;
}

// Common Comands
bool Database::flushAll() {
    std::lock_guard<std::mutex> lock(db_mutex); //get the mutex {thread safety} auto release when fxn exit RAII TYPE
    //clear the maps
    kv_store.clear();
    list_store.clear();
    hash_store.clear();

    //return success
    return true;
}

// key value ops 
void Database::set(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(db_mutex); //RAII auto release {get the lock}
    kv_store[key] = value; 
}

bool Database::get(const std::string& key, std::string& value) {
    //store retrieved value at &value ref
    std::lock_guard<std::mutex> lock(db_mutex); //get lock
    purgeExpired(); //remove expired keys 
    auto it = kv_store.find(key); //search in map
    if (it != kv_store.end()) {
        value = it->second; //put value at &value
        return true;//success
    }
    return false;//not found pointer reached at map.end()
}

//retreive all keys from keyvalue, list and hash maps
//gpt said raii lock good -> auto release when obj goes out of scope so be it
std::vector<std::string> Database::keys() {
    std::lock_guard<std::mutex> lock(db_mutex); //get the lock
    purgeExpired(); //expired keys out
    std::vector<std::string> result; 

    //iterate and store in result var
    for (const auto& pair : kv_store) {
        result.push_back(pair.first);
    }
    for (const auto& pair : list_store) {
        result.push_back(pair.first);
    }
    for (const auto& pair : hash_store) {
        result.push_back(pair.first);
    }

    //return result
    return result;
}


//get the type of key->string, list or hash
std::string Database::type(const std::string& key) {
    std::lock_guard<std::mutex> lock(db_mutex); //lock :thread safety 
    purgeExpired();

    //check in which db will find key 
    if (kv_store.find(key) != kv_store.end()) 
        return "string";
    if (list_store.find(key) != list_store.end())
        return "list";
    if (hash_store.find(key) != hash_store.end()) 
        return "hash";

    //not found anywhere 
    else return "none";    
}


//delete a key 
bool Database::del(const std::string& key) {
    std::lock_guard<std::mutex> lock(db_mutex);
    purgeExpired();
    bool erased = false; //status of key to be deleted
    erased |= kv_store.erase(key) > 0;
    erased |= list_store.erase(key) > 0;
    erased |= hash_store.erase(key) > 0;
    return erased; //return staus of deletion
}


//setting expirty time of a key 
bool Database::expire(const std::string& key, int seconds) {
    std::lock_guard<std::mutex> lock(db_mutex);
    purgeExpired();
    
    //first checking if key acutally exist lol
    bool exists = (kv_store.find(key) != kv_store.end()) ||
                  (list_store.find(key) != list_store.end()) ||
                  (hash_store.find(key) != hash_store.end());

    //if not exist then fuck it
    if (!exists)
        return false;
    
    //now() wont affected by system time {monotonic -> move forward} + add TTL to it
    expiry_map[key] = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    return true;//success
}


//remove expired keys
void Database::purgeExpired() {
    auto now = std::chrono::steady_clock::now(); //get current monotonic time

    //iterate in expiry_map 
    for (auto it = expiry_map.begin(); it != expiry_map.end(); ) {
        //if currentime > expired time set during key store
        if (now > it->second) {
            // attempt to remove from all stores
            kv_store.erase(it->first);
            list_store.erase(it->first);
            hash_store.erase(it->first);
            it = expiry_map.erase(it);
        } else {
            ++it;
        }
    }
}

//move value and expiration to newkey
bool Database::rename(const std::string& oldKey, const std::string& newKey) {
    std::lock_guard<std::mutex> lock(db_mutex);
    purgeExpired();
    bool found = false; //status that key is found 

    //attempt to find in all maps
    //in parallel because same key can be in multiple maps 
    auto itKv = kv_store.find(oldKey);
    if (itKv != kv_store.end()) {
        kv_store[newKey] = itKv->second;
        kv_store.erase(itKv);
        found = true;
    }

    auto itList = list_store.find(oldKey);
    if (itList != list_store.end()) {
        list_store[newKey] = itList->second;
        list_store.erase(itList);
        found = true;
    }

    auto itHash = hash_store.find(oldKey);
    if (itHash != hash_store.end()) {
        hash_store[newKey] = itHash->second;
        hash_store.erase(itHash);
        found = true;
    }

    //move expiry data to new key 
    auto itExpire = expiry_map.find(oldKey);
    if (itExpire != expiry_map.end()) {
        expiry_map[newKey] = itExpire->second;
        expiry_map.erase(itExpire);
    }

    return found;//return status
}





// List Opreations
//---------------



//reteive list stored against key 
std::vector<std::string> Database::lget(const std::string& key) {
    std::lock_guard<std::mutex> lock(db_mutex);
    auto it = list_store.find(key);
    if (it != list_store.end()) {
        return it->second; //return list
    }
    return {}; //return empty result
}

//get the length list stored agains ekey
ssize_t Database::llen(const std::string& key) {
    std::lock_guard<std::mutex> lock(db_mutex);
    auto it = list_store.find(key);
    if (it != list_store.end()) 
        return it->second.size(); //just the size
    return 0; //not found
}

//push at left of list
//if no list must create one (auto work)
void Database::lpush(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(db_mutex);
    list_store[key].insert(list_store[key].begin(), value);
}

//push at right
void Database::rpush(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(db_mutex);
    list_store[key].push_back(value);
}


//pop from left and return element 
bool Database::lpop(const std::string& key, std::string& value) {
    std::lock_guard<std::mutex> lock(db_mutex);
    auto it = list_store.find(key);
    //two cond -> it should not point to end and it's list should not be empty
    if (it != list_store.end() && !it->second.empty()) {
        value = it->second.front();
        it->second.erase(it->second.begin());
        return true;
    }
    return false;
}

//retrieve rightmost element from list and remove
bool Database::rpop(const std::string& key, std::string& value) {
    std::lock_guard<std::mutex> lock(db_mutex);
    auto it = list_store.find(key);
    //two cond -> it should not point to end and it's list should not be empty
    if (it != list_store.end() && !it->second.empty()) {
        value = it->second.back();
        it->second.pop_back();
        return true;
    }
    return false;
}

//remove count values from list stored at key index in list-map thats basically it
int Database::lrem(const std::string& key, int count, const std::string& value) {
    std::lock_guard<std::mutex> lock(db_mutex);
    int removed = 0; //counter how many removed
    auto it = list_store.find(key);
    if (it == list_store.end()) 
        return 0; //not removed

    auto& lst = it->second; 

    if (count == 0) {
        // iterate in list and remove all
        auto new_end = std::remove(lst.begin(), lst.end(), value); //move elements not equal to value to front of list

        removed = std::distance(new_end, lst.end()); //how many removed => diff of new end and old end pointer
        lst.erase(new_end, lst.end()); //remove elements from logical end to actual end {here all values = value}
    } else if (count > 0) {
        // Remove from head to tail
        //stopping condition -> removed == count as removed is 0 indexed 
        for (auto iter = lst.begin(); iter != lst.end() && removed < count; ) {
            if (*iter == value) {
                iter = lst.erase(iter);
                ++removed;
            } else {
                ++iter;
            }
        }
    } else {
        // count negative means remove from tail to head direction mod{count} number of values = value
        for (auto riter = lst.rbegin(); riter != lst.rend() && removed < (-count); ) {
            if (*riter == value) {
                auto fwdIter = riter.base(); //forward iterator pointing at i+1 if rev iterator points at i
                --fwdIter;//move one back i+1 -> i
                fwdIter = lst.erase(fwdIter); //erase
                ++removed;//inc count
                riter = std::reverse_iterator<std::vector<std::string>::iterator>(fwdIter);//convert forward iterator to reverse iterator iterating on vector of string bcz of list obviousl

            } else {
                ++riter; //move one back i -> i-1
            }
        }
    }
    return removed; //return count;
}


//need params -> value, index, key
//get value at index
bool Database::lindex(const std::string& key, int index, std::string& value) {
    std::lock_guard<std::mutex> lock(db_mutex);
    auto it = list_store.find(key);
    if (it == list_store.end()) 
        return false;//no index

    const auto& lst = it->second; //ref to vector of strings

    //count from left
    if (index < 0)
        index = lst.size() + index; //convert to normal index

    //invalid negative index provided -> exceeding list size after forward conversion
    //damn .size() return size_t {unsigned shit} must convert to int {signed shit}
    //but what if size exceed INT_MAX fuck it will see later
    if (index < 0 || index >= static_cast<int>(lst.size()))
        return false;

    //put value in the value var 
    value = lst[index];
    return true;//success
}

//set value at index in list store in key counterpart 
//damn too mmany safety checks should be done
bool Database::lset(const std::string& key, int index, const std::string& value) {
    std::lock_guard<std::mutex> lock(db_mutex);
    auto it = list_store.find(key);
    if (it == list_store.end()) 
        return false; //not found key

    auto& lst = it->second; //get ref to vector of string 
    if (index < 0)
        index = lst.size() + index; //nigga index -> normal index
    if (index < 0 || index >= static_cast<int>(lst.size()))
        return false;
    
    lst[index] = value; //set the value
    return true;
}

// Hash map<str,map> operations 
bool Database::hset(const std::string& key, const std::string& field, const std::string& value) {
    std::lock_guard<std::mutex> lock(db_mutex);
    hash_store[key][field] = value; //set value to field
    return true;
}

bool Database::hget(const std::string& key, const std::string& field, std::string& value) {
    std::lock_guard<std::mutex> lock(db_mutex);
    auto it = hash_store.find(key); //point iterator to map
    if (it != hash_store.end()) {
        auto f = it->second.find(field); //find field and point iterator to it
        if (f != it->second.end()) {
            value = f->second; //retreive data from pointer -> second and put in value ref
            return true; //success
        }
    }
    return false; //failure
}


//check if field exist
bool Database::hexists(const std::string& key, const std::string& field) {
    std::lock_guard<std::mutex> lock(db_mutex);
    auto it = hash_store.find(key);
    if (it != hash_store.end())
        return it->second.find(field) != it->second.end(); //return bool 
    return false;//failure -> only return when key not found not related to map counterpart of key
}

//clear field map at key 
bool Database::hdel(const std::string& key, const std::string& field) {
    std::lock_guard<std::mutex> lock(db_mutex);
    auto it = hash_store.find(key);
    if (it != hash_store.end())
        return it->second.erase(field) > 0; //success
    return false;//key not found
}

//get all field at given key 
std::unordered_map<std::string, std::string> Database::hgetall(const std::string& key) {
    std::lock_guard<std::mutex> lock(db_mutex);
    if (hash_store.find(key) != hash_store.end())
        return hash_store[key]; //return complete map
    return {}; //empty map
}

//all field names retreived stored at key 
std::vector<std::string> Database::hkeys(const std::string& key) {
    std::lock_guard<std::mutex> lock(db_mutex);
    std::vector<std::string> fields;
    auto it = hash_store.find(key);
    if (it != hash_store.end()) {
        for (const auto& pair: it->second)
            fields.push_back(pair.first);
    }
    return fields;
}

std::vector<std::string> Database::hvals(const std::string& key) {
    std::lock_guard<std::mutex> lock(db_mutex);
    std::vector<std::string> values;
    auto it = hash_store.find(key);
    if (it != hash_store.end()) {
        for (const auto& pair: it->second)
            values.push_back(pair.second);
    }
    return values;
}

ssize_t Database::hlen(const std::string& key) {
    std::lock_guard<std::mutex> lock(db_mutex);
    auto it = hash_store.find(key);
    return (it != hash_store.end()) ? it->second.size() : 0;
}

bool Database::hmset(const std::string& key, const std::vector<std::pair<std::string, std::string>>& fieldValues) {
    std::lock_guard<std::mutex> lock(db_mutex);
    for (const auto& pair: fieldValues) {
        hash_store[key][pair.first] = pair.second;
    }
    return true;
}

//--------------------
//--------------------




bool Database::dump(const std::string& filename) {
    std::lock_guard<std::mutex> lock(db_mutex);
    std::ofstream ofs(filename, std::ios::binary); //open in binary mode
    if (!ofs) return false;//if no permission return false

    //key value store
    // K name jaggiboss
    // K age 21


    for (const auto& kv: kv_store) {
        ofs << "K " << kv.first << " " << kv.second << "\n";
    }
    for (const auto& kv : list_store) {
        ofs << "L " << kv.first;
        for (const auto& item : kv.second)
            ofs << " " << item;
        ofs << "\n";
    }
    for (const auto& kv : hash_store) {
        ofs << "H " << kv.first;
        for (const auto& field_val : kv.second) 
            ofs << " " << field_val.first << ":" << field_val.second;
        ofs << "\n";
    }
    return true;
}

/*
Key-Value (K)
kv_store["name"] = "jaggi";
kv_store["city"] = "yash";

List (L)
list_store["fruits"] = {"apple", "banana", "orange"};
list_store["colors"] = {"red", "green", "blue"};

Hash (H)
hash_store["user:100"] = {
    {"name", "jaggi"},
    {"age", "21"},
    {"email", "jms21082003@gmail.com"}
};

hash_store["user:200"] = {
    {"name", "yash"},
    {"age", "21"},
    {"email", "yashbkl@gmail.com"}
};
*/
bool Database::load(const std::string& filename) {
    std::lock_guard<std::mutex> lock(db_mutex);
    std::ifstream ifs(filename, std::ios::binary);
    if (!ifs) return false;

    kv_store.clear();
    list_store.clear();
    hash_store.clear();

    std::string line;
    while (std::getline(ifs, line)) {
        std::istringstream iss(line);
        char type;
        iss >> type;
        if (type == 'K') {
            std::string key, value;
            iss >> key >> value;
            kv_store[key] = value;
        } else if (type == 'L') {
            std::string key;
            iss >> key;
            std::string item;
            std::vector<std::string> list;
            while (iss >> item)
                list.push_back(item);
            list_store[key] = list;
        } else if (type == 'H') {
            std::string key;
            iss >> key;
            std::unordered_map<std::string, std::string> hash;
            std::string pair;
            while (iss >> pair) {
                auto pos = pair.find(':');
                if (pos != std::string::npos) {
                    std::string field = pair.substr(0, pos);
                    std::string value = pair.substr(pos+1);
                    hash[field] = value;
                }
            }
            hash_store[key] = hash;
        }
    }
    return true;
}