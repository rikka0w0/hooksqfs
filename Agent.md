这个项目的目的是通过LD_PRELOAD实现对一个x86 32位程序的文件和文件夹访问进行拦截，然后把相应的访问转发到一个squashfs去。

背景：
1. 程序会运行在一个没有root权限和没有fuse的linux x86 32位环境下
2. 调试的时候会用linux x86 64位平台
3. 只考虑glibc的linux平台即可

代码样式(使用tab作为indentation)：
```c
void my_func(int arg1) {
	if (arg1 > 114514) {
		exit(1);
	}
}
```

在开发机（x86_64）上快速测试：
```bash
make clean
make BITS=64
LD_PRELOAD=$(pwd)/libhooksqfs.so /bin/sh -c "ls"
```

部署到目标机：
```bash
make clean
make BITS=32
scp -P24731 libhooksqfs.so container@theo.hidencloud.com:/tmp/libhooksqfs.so
```

srcds实际测试：
`LD_PRELOAD=/tmp/hook/libhooksqfs.so HOOKSQFS_LOG_EXCLUDE=access,readdir,readdir64,closedir ./srcds_run`
