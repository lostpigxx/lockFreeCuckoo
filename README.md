
Original: https://github.com/vigneshjathavara/Lock-Free-Cuckoo-Hashing.git

Modified by Liang Xiao.

当前问题：
1. resize/ensureCapacity存在并发性问题，即在resize的时候如果发生了插入或者删除操作，是可能出现位置后果的。（可以尝试在resize的时候锁定整个哈希表，但目前还没做）   
2. 哈希函数key只能针对整数，不能针对字符串(可以通过重写哈希函数hash1和hash2来完成)
3. Insert函数存在多次delete的问题，原因是多个线程会尝试同时销毁同一个对象，但是似乎不用锁难以解决该问题，目前置之不理，可能的后果是内存泄露，即随着插入的进行，会有一部分没有被释放的内存残留
