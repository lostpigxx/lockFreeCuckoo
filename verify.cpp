// test module for hashing

// this is an example, include your own .h file
#include "lockFreeCuckoo.h"
#include <iostream>
#include <vector>

using namespace std;

int main() {
    // some varaible, can be adjust by yourself
    const static int TABLE_SIZE = 16384 * 4;
    const static double LOAD_FACT = 0.4;
    const static int MAX_CHECK_TIMES = 8192 * 4;
    const static int MAX_DELETE_TIMES = 2048 * 4;
    const static int MAX_KEY_RANGE = 100000000;

    // define your table
    class lockFreeCuckoo<int> *myHash = new class lockFreeCuckoo<int>(TABLE_SIZE/2, TABLE_SIZE/2); // need customization
    // initial random seed
    srand((unsigned)time(NULL));

    int numKey = (int)(TABLE_SIZE * LOAD_FACT);

    // linear insertion 
	char *p = new char[6]{'h', 'e', 'l', 'l', 'o', 0};
    for (int i = 0; i < numKey; i++) {
        p[rand()%6] = ('a' + (rand()%26));
        myHash->Insert(p, i);
    }
    printf("linear insertion success!\n");

    // random existed key verify
    bool alldone = true;
    for (int i = 0; i < MAX_CHECK_TIMES; i++) {
        int key = rand() % numKey;
        if (myHash->Contains(key) == false) {
            printf("check %d failed \n", key);
            alldone = false;
        }
    }
    if (alldone) {
        printf("Random existed key verify, All check success!\n");
    }

    // random non-existed key verify
    alldone = true;
    for (int i = 0; i < MAX_CHECK_TIMES; i++) {
        int key = rand() % numKey + numKey;
        if (myHash->Contains(key) == true) {
            printf("check %d failed \n", key);
            alldone = false;
        }
    }
    if (alldone) {
        printf("random non-existed key verify, All check success!\n");
    }
    
    alldone = true;
    int counter = 0;
    // random deletion
    for (int i = 0; i < MAX_DELETE_TIMES; i++) {
        int key = rand() % numKey;
        if (myHash->Contains(key) == false)
            continue;
        myHash->Delete(key);
        counter++;
        if (myHash->Contains(key) == true)   {
            printf("delete %d failed\n", key);
            alldone = false;
        }
    }
	if (alldone) {
        printf("random deletion %d, All check success!\n", counter);
    }

    //----------------------------next term--------------------
    delete myHash;
    myHash = new class lockFreeCuckoo<int>(TABLE_SIZE/2, TABLE_SIZE/2); // need customization
    
    // random insertion
    vector<int> keys;
    p = new char[6]{'h', 'e', 'l', 'l', 'o', 0};
    counter = 0;
    for (int i = 0; i < numKey; i++) {
        int key = rand() % MAX_KEY_RANGE;
        p[rand()%6] = ('a' + (rand()%26));
        if (myHash->Contains(key) == true)
            continue;
        counter++;
        myHash->Insert(p, key);
        if (rand() % 2) {
            keys.push_back(key);
        }
    }
    printf("random insert complete, total keys = %d\n", counter);

    // random deletion and verify
    alldone = true;
    for(vector<int>::iterator it = keys.begin(); it != keys.end(); it++) {
        if (myHash->Contains(*it) == false) {
            alldone = false;
            printf("insertion for %d failed\n", *it);
            continue;
        }
        myHash->Delete(*it);
        if (myHash->Contains(*it) == true) {
            alldone = false;
            printf("deletion for %d failed\n", *it);
        }
    }
    if (alldone) {
        printf("random deletion and verify, All check success!\n");
    }
    
}