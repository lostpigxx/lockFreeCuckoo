
Original: https://github.com/vigneshjathavara/Lock-Free-Cuckoo-Hashing.git

Modified by Liang Xiao.

当前问题：
1. resize/ensureCapacity存在并发性问题（可以尝试在resize的时候锁定整个哈希表，但目前还没做）   
2. 哈希函数key只能针对整数，不能针对字符串(可以通过重写哈希函数来完成)
3. Insert函数存在多次delete的问题（原因是多个线程会尝试同时销毁同一个对象，目前置之不理，存在内存泄露，即每次插入原先的Entry并不会被销毁，正在想办法）
