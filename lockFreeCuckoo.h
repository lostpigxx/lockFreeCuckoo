/*
 * 无锁杜鹃哈希
 * 当前问题：
 * 1. resize/ensureCapacity存在并发性问题（可以尝试在resize的时候锁定整个哈希表，但目前还没做）   
 * 2. 哈希函数key只能针对整数，不能针对字符串(可以通过重写哈希函数来完成)
 * 3. Insert函数存在多次delete的问题
 *   （原因是多个线程会尝试同时销毁同一个对象，目前置之不理，存在内存泄露，即每次插入原先的Entry并不会被销毁，正在想办法）
 */
 #include <stdio.h>
 #include <stdlib.h>
 #include <iostream>
 #include <atomic>
 #include <thread>
 #include <memory>

using namespace std;

/*---------------------------------------------------------------------------*/

template <class KeyType> 
class KeyComparator {
public:
// Compare a and b. Return a negative value if a is less than b, 0 if they
// are equal, and a positive value if a is greater than b
    // virtual int operator()(const KeyType &a, const KeyType &b) const = 0;
    int operator()(const KeyType &a, const KeyType &b) const {
        if (a < b)
            return -1;
        if (a == b)
            return 0;
        return 1;
    }
};

template <class KeyType>
class Entry {
public:
	KeyType key;
	char* value;
	Entry<KeyType>(const KeyType &k, const char* v);
	~Entry<KeyType>();
};

template <class KeyType>
Entry<KeyType>::Entry(const KeyType &k, const char* v) : key(k) {
	value = new char[strlen(v) + 1];
	strcpy(value, v);
}

template <class KeyType>
Entry<KeyType>::~Entry() {
	delete value;
}

template <class KeyType>
class lockFreeCuckoo {
private:
    Entry<KeyType> *cursor;
    static const int FIRST = 0;
    static const int SECOND = 1;
    static const int NIL = -1;
public:
    std::atomic<Entry<KeyType> *> *table1;
    std::atomic<Entry<KeyType> *> *table2;
    size_t t1Size;
    size_t t2Size;

	lockFreeCuckoo(size_t size1, size_t size2);
	~lockFreeCuckoo();
    char* Search(KeyType key);
	bool Insert(const char* address, const KeyType &key);    
    bool Delete(const KeyType &key);
    bool Contains(const KeyType &key) const;

    bool moveToKey(const KeyType &searchKey);
    char* nextValueAtKey();

    size_t getSize();
    void ensureCapacity(uint32_t capacity);		
    void printTable();

private:
    void init();
    int Find(KeyType key, Entry<KeyType> **ent1, Entry<KeyType> **ent2);
    bool Relocate(int tableNum , int pos);
    void helpRelocate(int table , int idx, bool initiator);
    void deleteDup(int idx1, Entry<KeyType> *ent1,int idx2,Entry<KeyType> *ent2);
    
    int hash1(KeyType key);
    int hash2(KeyType key);

private:
	// KeyComparator不需要大家实现，但是key的比较需要由compare_来完成
	KeyComparator<KeyType> const compare_;						

};