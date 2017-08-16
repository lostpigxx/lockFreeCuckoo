#include "lockFreeCuckoo.h"

using namespace std;

// a 64-bits pointer only use low 48-bits
// we use high 16-bits to store the counter
inline int get_cnt(void* ptr) {
	unsigned long a = ((unsigned long)ptr & (0xffff000000000000));
	return a >> 48;
}

template <class KeyType>
inline void inc_counter(Entry<KeyType>** ptr) {
	*ptr = (Entry<KeyType> *)((unsigned long)*ptr + 0x0001000000000000);
}

template <class KeyType>
inline void store_count(Entry<KeyType>** ptr, int cnt) {
	unsigned long new_cnt = (unsigned long)cnt << 48;
	*ptr = (Entry<KeyType> *)((unsigned long)*ptr & 0x0000FFFFFFFFFFFF);
	*ptr = (Entry<KeyType> *)((unsigned long)*ptr + new_cnt);
}

// the last 2-bits of Entrypointer is always zero.
template <class KeyType>
inline Entry<KeyType>* extract_address(Entry<KeyType> *e) {
	e = (Entry<KeyType> *)((unsigned long)e & (0x0000fffffffffffc));
	return e;
}

bool is_marked(void *ptr) {
	if((unsigned long)ptr & 0x01)
		return true;
	else
		return false;
}

bool checkCounter(int ctr1,int ctr2, int ctrs1, int ctrs2) {
	if((ctrs1 - ctr1) >= 2 && (ctrs2 - ctr1) >= 2)
		return true;
	return false;
}


template <class KeyType>
lockFreeCuckoo<KeyType>::lockFreeCuckoo(size_t size1, size_t size2) : t1Size(size1), t2Size(size2) {
    table1 = new std::atomic<Entry<KeyType> *>[size1];
    table2 = new std::atomic<Entry<KeyType> *>[size2];
    cursor = NULL;
    init();
}

template <class KeyType>
lockFreeCuckoo<KeyType>::~lockFreeCuckoo() {
	delete table1;
	delete table2;
}

template <class KeyType>
void lockFreeCuckoo<KeyType>::init() {
	Entry<KeyType> *temp = NULL;
	for(int i = 0; i < t1Size; i++) {
		atomic_store(&table1[i], temp);
	}

	for(int i = 0; i < t2Size; i++) {
		atomic_store(&table2[i], temp);
	}
}

template <class KeyType>
int lockFreeCuckoo<KeyType>::hash1(KeyType key) {
	int num = 0x27d4eb2d; // a prime or an odd constant
	key = (key ^ 61) ^ (key >> 16);
	key = key + (key << 3);
	key = key ^ (key >> 4);
	key = key * num;
	key = key ^ (key >> 15);
	return key % t1Size;
}

template <class KeyType>
int lockFreeCuckoo<KeyType>::hash2(KeyType key) {
	key = ((key >> 16) ^ key) * 0x45d9f3b;
 	key = ((key >> 16) ^ key) * 0x45d9f3b;
  	key = (key >> 16) ^ key;
  	return key % t2Size;
}

template <class KeyType>
void lockFreeCuckoo<KeyType>::helpRelocate(int which, int idx, bool initiator) {
	Entry<KeyType> *srcEntry, *dstEntry, *tmpEntry;
	int dstIdx, nCnt, cnt1, cnt2, size[2];
	atomic<Entry<KeyType> *> *tbl[2];
	tbl[0] = table1;
	tbl[1] = table2;
	size[0] = t1Size;
	size[1] = t2Size;
	while(true) {
		srcEntry = atomic_load_explicit(&tbl[which][idx], memory_order_seq_cst);
		//Marks the Entry to logically swap it
		while(initiator && !(is_marked((void *)srcEntry))) {
			if(extract_address(srcEntry) == NULL)
				return;
			// mark the flag
			tmpEntry = (Entry<KeyType> *)((unsigned long)srcEntry | 1); 
			atomic_compare_exchange_strong(&(tbl[which][idx]), &srcEntry, tmpEntry);
			srcEntry = atomic_load_explicit(&tbl[which][idx], memory_order_seq_cst);
		}
		if(!(is_marked((void *)srcEntry)))
			return;

		int key = extract_address(srcEntry)->key;
		dstIdx = (which == FIRST ? hash2(key) : hash1(key));
		dstEntry = atomic_load_explicit(&tbl[1-which][dstIdx], memory_order_seq_cst);
		if(extract_address(dstEntry) == NULL) {
			cnt1 = get_cnt((void *)srcEntry);
			cnt2 = get_cnt((void *)dstEntry);
			nCnt = cnt1 > cnt2 ? cnt1 + 1 : cnt2 + 1;
			if(srcEntry != atomic_load_explicit(&tbl[which][idx], memory_order_seq_cst))
				continue;

			Entry<KeyType> *tmpSrcEntry = srcEntry;
			// unmark the flag
			tmpSrcEntry = (Entry<KeyType> *)((unsigned long)srcEntry & ~1);
			store_count(&tmpSrcEntry, nCnt);
			tmpEntry = NULL;
			store_count(&tmpEntry,cnt1 + 1);
			if(atomic_compare_exchange_strong(&(tbl[1-which][dstIdx]), &dstEntry, tmpSrcEntry))
				atomic_compare_exchange_strong(&(tbl[which][idx]), &srcEntry, tmpEntry);
			return;
		}

		if(srcEntry == dstEntry) {
			tmpEntry = NULL;
			store_count(&tmpEntry, cnt1 + 1);
			atomic_compare_exchange_strong(&(tbl[which][idx]), &srcEntry, tmpEntry);
			return;
		}
		tmpEntry = (Entry<KeyType> *)((unsigned long)srcEntry & (~1));
		store_count(&tmpEntry, cnt1 + 1);
		atomic_compare_exchange_strong(&(tbl[which][idx]), &srcEntry, tmpEntry);
		return;
	}
}


template <class KeyType>
void lockFreeCuckoo<KeyType>::deleteDup(int idx1, Entry<KeyType> *ent1, int idx2, Entry<KeyType> *ent2) {
	Entry<KeyType> *tmp1, *tmp2;
	int key1, key2, cnt;
	tmp1 = atomic_load(&table1[idx1]);
	tmp2 = atomic_load(&table2[idx2]);
	if((ent1 != tmp1) && (ent2 != tmp2))
		return;
	key1 = extract_address(ent1)->key;
	key2 = extract_address(ent2)->key;
	if(key1 != key2)
		return;
	tmp2 = NULL;
	cnt = get_cnt(ent2);
	store_count(&tmp2, cnt + 1);
	atomic_compare_exchange_strong(&(table2[idx2]), &ent2, tmp2);
}

template <class KeyType>
char* lockFreeCuckoo<KeyType>::Search(KeyType key)
{
	int h1 = hash1(key);
	int h2 = hash2(key);
	int cnt1, cnt2, ncnt1, ncnt2;
	while(true) {
		// 1st round
		// Looking in table 1
		Entry<KeyType> *ent1 = atomic_load_explicit(&table1[h1], memory_order_relaxed);
		cnt1 = get_cnt(ent1);
		ent1 = extract_address(ent1);
		if(ent1 != NULL && !compare_(ent1->key, key))
			return ent1->value;
		// Looking in table 2
		Entry<KeyType> *ent2 = atomic_load_explicit(&table2[h2], memory_order_relaxed);
		cnt2 = get_cnt(ent2);
		ent2 = extract_address(ent2);
		if(ent2 != NULL && !compare_(ent2->key, key))
			return ent2->value;

		// 2nd round
		ent1 = atomic_load_explicit(&table1[h1], memory_order_relaxed);
		ncnt1 = get_cnt(ent1);
		ent1 = extract_address(ent1);
		if(ent1 != NULL && !compare_(ent1->key, key))
			return ent1->value;
		ent2 = atomic_load_explicit(&table2[h2], memory_order_relaxed);
		ncnt2 = get_cnt(ent2);
		ent2 = extract_address(ent2);
		if(ent2 != NULL && !compare_(ent2->key, key))
			return ent2->value;

		if(checkCounter(cnt1,cnt2,ncnt1,ncnt2))
			continue;
		else 
			return NIL;
	}
}

template <class KeyType>
int lockFreeCuckoo<KeyType>::Find(KeyType key, Entry<KeyType> **ent1, Entry<KeyType> **ent2)
{
	int h1 = hash1(key);
	int h2 = hash2(key);
	int result = NIL;
	int cnt1, cnt2, ncnt1, ncnt2;
	Entry<KeyType> *e;
	while(true) {
		// 1st round
		e = atomic_load_explicit(&table1[h1], memory_order_relaxed);
		*ent1 = e;
		cnt1 = get_cnt(e);
		e = extract_address(e);
		// helping other concurrent thread if its not completely done with its job
		if(e != NULL) {
			if(is_marked((void*)e))	{
				helpRelocate(0, h1, false);
				continue;
			}
			else if(!compare_(e->key, key))
				result = FIRST;
		}

		e = atomic_load_explicit(&table2[h2], memory_order_relaxed);
		*ent2 = e;
		cnt2 = get_cnt(e);
		e = extract_address(e);
		// helping other concurrent thread if its not completely done with its job
		if(e != NULL) {
			if(is_marked((void*)e))	{
				helpRelocate(1, h2, false);
				continue;
			}
			if(!compare_(e->key, key)) {
				if(result == FIRST) {
					printf("Find(): Delete_dup()\n");
					deleteDup(h1, *ent1, h2, *ent2);
				}
				else
			   		result = SECOND;
			}
		}
		if(result == FIRST || result == SECOND)
			return result;

		// 2nd round
		e = atomic_load_explicit(&table1[h1], memory_order_relaxed);
		*ent1 = e;
		ncnt1 = get_cnt(e);
		e = extract_address(e);
		if(e != NULL) {
			if(is_marked((void*)e))	{
				helpRelocate(0, h1, false);
				printf("Find(): help_relocate()");
				continue;
			}
			else if(!compare_(e->key, key))
				result = FIRST;
		}

		e = atomic_load_explicit(&table2[h2], memory_order_relaxed);
		*ent2 = e;
		ncnt2 = get_cnt(e);
		e = extract_address(e);
		if(e != NULL) {
			if(is_marked((void*)e))	{
				helpRelocate(1, h2, false);
				continue;
			}
			if(!compare_(e->key, key)) {
				if( result == FIRST){
					printf("Find(): Delete_dup()\n");
					deleteDup(h1, *ent1, h2, *ent2);
				}
				else
			    	result = SECOND;
			}
		}
		if(result == FIRST || result == SECOND)
			return result;

		if(checkCounter(cnt1, cnt2, ncnt1, ncnt2))
			continue;
		else 
			return NIL;
	}
}

template <class KeyType>
bool lockFreeCuckoo<KeyType>::Relocate(int which, int index)
{
	int threshold = t1Size + t2Size;
	int route[threshold]; // store cuckoo path
	int startLevel = 0, tblNum = which, key;
	int idx = index, preIdx = 0;
	Entry<KeyType> *curEntry = NULL;
	Entry<KeyType> *preEntry = NULL;
	atomic<Entry<KeyType> *> *tbl[2];
	tbl[0] = table1;
	tbl[1] = table2;

	//discovery cuckoo path
path_discovery:
	bool found = false;
	int depth = startLevel;
	do {
		curEntry = atomic_load(&tbl[tblNum][idx]);
		while(is_marked((void *)curEntry)) {
			helpRelocate(tblNum, idx, false);
			curEntry = atomic_load(&tbl[tblNum][idx]);
		}

		Entry<KeyType> *preEntAddr, *curEntAddr;
		preEntAddr = extract_address(preEntry);
		curEntAddr = extract_address(curEntry);
		if(curEntAddr != NULL && preEntAddr != NULL) {
			if(preEntry == curEntry || !compare_(preEntAddr->key, curEntAddr->key)) {
				if(tblNum == FIRST)
					deleteDup(idx, curEntry, preIdx, preEntry);
				else
					deleteDup(preIdx, preEntry, idx, curEntry);
			}
		}
		// not an empty slot, continue discovery
		if(curEntAddr != NULL) {
			route[depth] = idx;
			key = curEntAddr->key;
			preEntry = curEntry;
			preIdx = idx;
			tblNum = 1 - tblNum; // change to another table
			idx = (tblNum == FIRST ? hash1(key) : hash2(key));
		}
		// find an empty slot
		else {
			found = true;
		}
	} while(!found && ++depth < threshold);

	// insert key
	if(found) {
		Entry<KeyType> *srcEntry, *dstEntry;
		int dstIdx;

		tblNum = 1 - tblNum;
		for(int i = depth - 1; i >= 0; --i, tblNum = 1 - tblNum) {
			idx = route[i];
			srcEntry = atomic_load(&tbl[tblNum][idx]);
			if(is_marked((void *)srcEntry)) {
				helpRelocate(tblNum, idx, false);
				srcEntry = atomic_load(&tbl[tblNum][idx]);
			}

			Entry<KeyType> *srcEntryAddr = extract_address(srcEntry);
			if(srcEntryAddr == NULL) {
				continue;
			}
			key = srcEntryAddr->key;
			dstIdx = (tblNum == FIRST ? hash2(key) : hash1(key));
			dstEntry = atomic_load(&tbl[1-tblNum][dstIdx]);
			if(extract_address(dstEntry) != NULL) {
				startLevel = i + 1;
				idx = dstIdx;
				tblNum = 1 - tblNum;
				goto path_discovery;
			}
			helpRelocate(tblNum, idx, true);
		}
	}
	return found;
}

template <class KeyType>
bool lockFreeCuckoo<KeyType>::Insert(const char* address, const KeyType &key) {
	Entry<KeyType> *newEntry = new Entry<KeyType>(key, address);
	Entry<KeyType> *ent1 = NULL, *ent2 = NULL;
	// shared_ptr<Entry<KeyType>> spent1(ent1); 
	// shared_ptr<Entry<KeyType>> spent2(ent2); 
	int cnt = 0;
	int h1 = hash1(key);
	int h2 = hash2(key);

	while(true) {
		int result = Find(key, &ent1, &ent2);
		//updating existing content
		if(result == FIRST) {
			cnt = get_cnt(ent1);
			store_count(&newEntry, cnt + 1);
			bool casResult = atomic_compare_exchange_strong(&(table1[h1]), &ent1, newEntry);
			if(casResult == true)
				return true;
			else
				continue;
		}
		if(result == SECOND) {
			cnt = get_cnt(ent2);
			store_count(&newEntry, cnt + 1);
			bool casResult = atomic_compare_exchange_strong(&(table2[h2]), &ent2, newEntry);
			if(casResult == true)
				return true;
			else
				continue;
		}
		// avoiding double duplicate instance of key
		// always insert to table 1 first.
		if(extract_address(ent1) == NULL && extract_address(ent2) == NULL) {
			cnt = get_cnt(ent1);
			store_count(&newEntry, cnt + 1);
			bool casResult = atomic_compare_exchange_strong(&(table1[h1]), &ent1, newEntry);
			if(casResult == true)
				return true;
			else
				continue;
		}

		if(extract_address(ent1) == NULL) {
			cnt = get_cnt(ent1);
			store_count(&newEntry, cnt + 1);
			bool casResult = atomic_compare_exchange_strong(&(table1[h1]), &ent1, newEntry);
			if(casResult == true)
				return true;
			else
				continue;
		}

		if(extract_address(ent2) == NULL) {
			cnt = get_cnt(ent2);
			store_count(&newEntry, cnt + 1);
			bool casResult = atomic_compare_exchange_strong(&(table2[h2]), &ent2, newEntry);
			if(casResult == true)
				return true;
			else
				continue;
		}
		bool relocateResult = Relocate(FIRST, h1);
		if(relocateResult == true) {
			continue;
		}
		else {
			//TODO: rehash
			printf("insert %d failed! need rehash or resize!\n", key);
			return false;
		}
	}
}


// template <class KeyType>
// bool lockFreeCuckoo<KeyType>::Insert(const char* address, const KeyType &key) {
//     Entry<KeyType> *newEntry = new Entry<KeyType>(key, address);
// 	Entry<KeyType> *ent1 = NULL, *ent2 = NULL;
// 	int cnt = 0;
// 	int h1 = hash1(key);
// 	int h2 = hash2(key);

// 	while(true) {
// 		int result = Find(key, &ent1, &ent2);
// 		//updating existing content
// 		if(result == FIRST) {
// 			cnt = get_cnt(ent1);
// 			store_count(&newEntry, cnt + 1);
// 			bool casResult = atomic_compare_exchange_strong(&(table1[h1]), &ent1, newEntry);
// 			// if (extract_address(ent1))
// 			// 	delete[] extract_address(ent1);	
// 			if(casResult == true)
// 				return true;
// 			else
// 				continue;
// 		}
// 		if(result == SECOND) {
// 			cnt = get_cnt(ent2);
// 			store_count(&newEntry, cnt + 1);
// 			bool casResult = atomic_compare_exchange_strong(&(table2[h2]), &ent2, newEntry);
// 			// if (extract_address(ent2))
// 			// 	delete[] extract_address(ent2);
// 			if(casResult == true)
// 				return true;
// 			else
// 				continue;
// 		}
// 		// avoiding double duplicate instance of key
// 		// always insert to table 1 first.
// 		if(extract_address(ent1) == NULL && extract_address(ent2) == NULL) {
// 			cnt = get_cnt(ent1);
// 			store_count(&newEntry, cnt + 1);
// 			bool casResult = atomic_compare_exchange_strong(&(table1[h1]), &ent1, newEntry);
// 			if(casResult == true)
// 				return true;
// 			else
// 				continue;
// 		}

// 		if(extract_address(ent1) == NULL) {
// 			cnt = get_cnt(ent1);
// 			store_count(&newEntry, cnt + 1);
// 			bool casResult = atomic_compare_exchange_strong(&(table1[h1]), &ent1, newEntry);
// 			// if (extract_address(ent1))
// 			// 	delete[] extract_address(ent1);
// 			if(casResult == true)
// 				return true;
// 			else
// 				continue;
// 		}

// 		if(extract_address(ent2) == NULL) {
// 			cnt = get_cnt(ent2);
// 			store_count(&newEntry, cnt + 1);
// 			bool casResult = atomic_compare_exchange_strong(&(table2[h2]), &ent2, newEntry);
// 			// if (extract_address(ent2))
// 			// 	delete[] extract_address(ent2);
// 			if(casResult == true)
// 				return true;
// 			else
// 				continue;
// 		}
// 		bool relocateResult = Relocate(FIRST, h1);
// 		if(relocateResult == true) {
// 			continue;
// 		}
// 		else {
// 			//TODO: rehash
// 			printf("insert %d failed! need rehash or resize!\n", key);
// 			return false;
// 		}
// 	}
// }

template <class KeyType>
bool lockFreeCuckoo<KeyType>::Delete(const KeyType &key) {
    Entry<KeyType> *ent1 = NULL, *ent2 = NULL;
  	int cnt=0;
  	int h1 = hash1(key);
  	int h2 = hash2(key);
	while(true) {
		int result = Find(key, &ent1, &ent2);
		if(result == FIRST) {
			Entry<KeyType> *tmp = NULL;
			cnt = get_cnt(ent1);
			store_count(&tmp, cnt + 1);
			bool casResult = atomic_compare_exchange_strong(&(table1[h1]), &ent1, tmp);
			if(casResult == true)
				return true;
			else
				continue;
		}
		else if(result == SECOND) {
			if(table1[h1] != ent1){
				continue;
			}
			Entry<KeyType> *tmp = NULL;
			cnt = get_cnt(ent2);
			store_count(&tmp, cnt + 1);
			bool casResult = atomic_compare_exchange_strong(&(table2[h2]), &ent2, tmp);
			if(casResult == true)
				return true;
			else
				continue;
		}
		else {
			return false;
		}
    }
}

template <class KeyType>
bool lockFreeCuckoo<KeyType>::Contains(const KeyType &key) const {
	int h1 = hash1(key);
	int h2 = hash2(key);
	int cnt1, cnt2, ncnt1, ncnt2;
	while(true) {
		// 1st round
		// Looking in table 1
		Entry<KeyType> *ent1 = atomic_load_explicit(&table1[h1], memory_order_relaxed);
		cnt1 = get_cnt(ent1);
		ent1 = extract_address(ent1);
		if(ent1 != NULL && !compare_(ent1->key, key))
			return true;
		// Looking in table 2
		Entry<KeyType> *ent2 = atomic_load_explicit(&table2[h2], memory_order_relaxed);
		cnt2 = get_cnt(ent2);
		ent2 = extract_address(ent2);
		if(ent2 != NULL && !compare_(ent2->key, key))
			return true;
		// 2nd round
		ent1 = atomic_load_explicit(&table1[h1], memory_order_relaxed);
		ncnt1 = get_cnt(ent1);
		ent1 = extract_address(ent1);
		if(ent1 != NULL && !compare_(ent1->key, key))
			return true;
		ent2 = atomic_load_explicit(&table2[h2], memory_order_relaxed);
		ncnt2 = get_cnt(ent2);
		ent2 = extract_address(ent2);
		if(ent2 != NULL && !compare_(ent2->key, key))
			return true;

		if(checkCounter(cnt1,cnt2,ncnt1,ncnt2))
			continue;
		else 
			return false;
	}
}




template <class KeyType>
void lockFreeCuckoo<KeyType>::printTable()
{
	printf("******************hash_table 1*****************\n");
	Entry<KeyType> *e, *tmp = NULL;
	for(int i = 0; i < t1Size; i++){
		if(table1[i] != NULL) {
			e = atomic_load_explicit(&table1[i], memory_order_relaxed);
			tmp = extract_address(e);
			if(tmp != NULL)
				printf("%d\t%016lx\t%d\t%s\n", i, (long)e, tmp->key, tmp->value);
			else
				printf("%d\t%016lx\n", i, (long)e);
		}
		else {
			printf("%d\tNULL\n",i);
		}	
	}
	printf("****************hash_table 2*******************\n");
	for(int i = 0; i < t2Size; i++){
		if(table2[i] != NULL) {
			e = atomic_load_explicit(&table2[i], memory_order_relaxed);
			tmp = extract_address(e);
			if(tmp != NULL)
				printf("%d\t%016lx\t%d\t%s\n", i, (long)e, tmp->key, tmp->value);
			else
				printf("%d\t%016lx\n", i, (long)e);
		}
		else {
			printf("%d\tNULL\n", i);
		}
	}
	printf("\n");
}

template <class KeyType>
bool lockFreeCuckoo<KeyType>::moveToKey(const KeyType &searchKey) {
    int h1 = hash1(searchKey);
	int h2 = hash2(searchKey);
	int cnt1, cnt2, ncnt1, ncnt2;
	while(true) {
		// 1st round
		// Looking in table 1
		Entry<KeyType> *ent1 = atomic_load_explicit(&table1[h1], memory_order_relaxed);
		cnt1 = get_cnt(ent1);
		ent1 = extract_address(ent1);
		if(ent1 != NULL && !compare_(ent1->key, searchKey)) {
            this->cursor = ent1;
            return true;
        }
		// Looking in table 2
		Entry<KeyType> *ent2 = atomic_load_explicit(&table2[h2], memory_order_relaxed);
		cnt2 = get_cnt(ent2);
		ent2 = extract_address(ent2);
		if(ent2 != NULL && !compare_(ent2->key, searchKey)) {
            this->cursor = ent2;
            return true;
        }

		// 2nd round
		ent1 = atomic_load_explicit(&table1[h1], memory_order_relaxed);
		ncnt1 = get_cnt(ent1);
		ent1 = extract_address(ent1);
		if(ent1 != NULL && !compare_(ent1->key, searchKey)) {
            this->cursor = ent1;
            return true;
        }
		ent2 = atomic_load_explicit(&table2[h2], memory_order_relaxed);
		ncnt2 = get_cnt(ent2);
		ent2 = extract_address(ent2);
		if(ent2 != NULL && !compare_(ent2->key, searchKey)) {
            this->cursor = ent2;
            return true;
        }

		if(checkCounter(cnt1,cnt2,ncnt1,ncnt2))
			continue;
		else 
			return false;
	}
}

template <class KeyType>
char* lockFreeCuckoo<KeyType>::nextValueAtKey() {
	if (this->cursor == NULL)
		return new char(0);
    return this->cursor->value;
}

template <class KeyType>
size_t lockFreeCuckoo<KeyType>::getSize() {
    return t1Size + t2Size;
}
// Only for hash index to reSize and rehash
// 默认2个表分别为capacity/2
template <class KeyType>
void lockFreeCuckoo<KeyType>::ensureCapacity(uint32_t capacity) {
	std::atomic<Entry<KeyType> *> *oldTable1 = table1;
	std::atomic<Entry<KeyType> *> *oldTable2 = table2;
	size_t old1 = t1Size;
	size_t old2 = t2Size;
	table1 = new std::atomic<Entry<KeyType> *>[capacity/2];
	table2 = new std::atomic<Entry<KeyType> *>[capacity/2];
	t1Size = capacity / 2;
	t2Size = capacity / 2;
	cursor = NULL;
	Entry<KeyType> *temp = NULL;
	for (int i = 0; i < t1Size; i++) {
		atomic_store(&table1[i], temp);
	}

	for (int i = 0; i < t2Size; i++) {
		atomic_store(&table2[i], temp);
	}

	// insert old value.
	for (int i = 0; i < old1; i++) {
		temp = extract_address(atomic_load_explicit(&oldTable1[i], memory_order_relaxed));
		if (temp)
			Insert(temp->value, temp->key);
			
	}
	for (int i = 0; i < old2; i++) {
		temp = extract_address(atomic_load_explicit(&oldTable2[i], memory_order_relaxed));
		if (temp)
			Insert(temp->value, temp->key);
	}

	delete oldTable1;
	delete oldTable2;
	
}


//single test
int main() {
    class lockFreeCuckoo<int> myHash(32, 32);
	//myHash.printTable();
	char *p = new char[6]{'h', 'e', 'l', 'l', 'o', 0};
    for (int i = 0; i < 50; i++) {
        myHash.Insert(p, i);
    }
	//myHash.printTable();
	myHash.Delete(8);
	myHash.printTable();
	// cout << myHash.nextValueAtKey() << endl;
	// myHash.moveToKey(40);
	// cout << myHash.nextValueAtKey() << endl;
	// cout << myHash.getSize() << endl;
	myHash.ensureCapacity(128);
	myHash.printTable();
}



// multiple thread
// lockFreeCuckoo<int> mh(128, 128);

// void* test0(void*) {
// 	//mh.printTable();
// 	char *p = new char[6]{'h', 'e', 'l', 'l', 'o', 0};
// 	for (int i = 128; i >= 0; i--) {
// 		mh.Insert(p, i);
// 	}
// 	return NULL;
// }

// void* test1(void*) {
// 	// mh.printTable();
// 	char *p = new char[6]{'w', 'o', 'r', 'l', 'd', 0};
// 	for (int i = 1; i < 128; i++) {
// 		mh.Insert(p, i);
// 	}
// 	// for (int i = 128; i >= 0; i--) {
// 	// 	mh.Delete(i);
// 	// }
// 	return NULL;
// }

// int main()
// {
// 	int rc, i;
// 	pthread_t threads[2];

// 	cout << "main() : creating thread,1 " << endl;
// 	rc = pthread_create(&threads[0], NULL, test0, NULL);
// 	if (rc) {
// 	    cout << "Error:unable to create thread," << rc << endl;
// 	    exit(-1);
// 	}

// 	cout << "main() : creating thread,2 " << endl;
// 	rc = pthread_create(&threads[1], NULL, test1, NULL);
// 	if (rc) {
// 	    cout << "Error:unable to create thread," << rc << endl;
// 	    exit(-1);
// 	}

// 	pthread_join(threads[0], NULL);
// 	pthread_join(threads[1], NULL);
// 	printf("End\n");
// 	mh.printTable();
// 	return 1;
// }