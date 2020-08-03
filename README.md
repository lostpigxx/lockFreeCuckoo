### 下一步计划
1. 添加insert过程中的自动resize
2. 使用专一的线程来监控哈希表的性能
3. 想到了，可以用智能指针来解决问题3
4. 写个makefile吧，现在也太挫了


Original: https://github.com/vigneshjathavara/Lock-Free-Cuckoo-Hashing.git

Modified by Liang Xiao.   


编译参数   
> g++ -std=c++11

当前问题：
1. resize/ensureCapacity存在并发性问题，即在resize的时候如果发生了插入或者删除操作，是可能出现位置后果的。（可以尝试在resize的时候锁定整个哈希表，但目前还没做）   
2. 哈希函数key只能针对整数，不能针对字符串(可以通过重写哈希函数hash1和hash2来完成)
3. Insert函数存在多次delete的问题，原因是多个线程会尝试同时销毁同一个对象，但是似乎不用锁难以解决该问题，目前置之不理，可能的后果是内存泄露，即随着插入的进行，会有一部分没有被释放的内存残留


关于测试verify.cpp   
里面包含了顺序测试和随机写入删除测试，如果通过测试会显示类似于下面的文字
```
linear insertion success!
Random existed key verify, All check success!
random non-existed key verify, All check success!
random deletion 7034, All check success!
random insert complete, total keys = 26208
random deletion and verify, All check success!
```
使用方法   
include你自己的哈希表的.h文件，并且修改声明表的语句，使其通过编译即可。   
目前版本比较简陋，会根据需求逐步修改，有问题请直接联系我。