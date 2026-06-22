这个项目的目的是通过LD_PRELOAD实现对一个x86 32位程序的文件和文件夹访问进行拦截，然后把相应的访问转发到一个squashfs去。
我需要实现的效果类似于squashfs+overlayfs，其中squashfs作为只读的lower，实际文件系统作为可读写的upper。
不采用fuse或者内核去mount squashfs和overlayfs的原因是目标系统没有root也没有mount fuse的权限，因此只能采用全用户态去模拟。

背景：
1. 目标程序会运行在一个没有root权限和没有fuse的linux x86 32位环境下
2. 调试的时候可能会用linux x86 64位平台
3. 只考虑glibc的linux平台即可
4. 目标程序通过LD_PRELOAD加载我们的so文件
5. 我们的so文件会劫持特定libc函数实现我们的目的

不使用LD_PRELOAD去覆盖libc导出的符号，而是使用Trampoline hook修改特定libc函数的入口处的汇编代码，这样的好处是能劫持libc内部对这个函数的调用，减少整体需要hook的函数，另外Trampoline hook一旦被安装就不再需要修改入口处的汇编代码，可以确保多线程下依然可用。

环境变量HOOKSQFS_FILE用于指定squashfs镜像文件的绝对路径。
环境变量HOOKSQFS_PREFIX用于指定模拟overlayfs的绝对路径。
当被读的文件路径或者文件夹路径不属于HOOKSQFS_PREFIX以及其子目录时，fallback到实际文件系统的读操作。
当被读的文件路径或者文件夹路径属于HOOKSQFS_PREFIX以及其子目录时，首先尝试实际文件系统的读操作，如果失败（比如文件不存在）则尝试从squashfs镜像对应目录读，如果实际文件系统和squashfs镜像都没有这个文件或者文件夹，则读取失败。
如果一个文件同时存在于实际文件系统和squashfs镜像中，采用实际文件。
如果一个文件夹同时存在于实际文件系统和squashfs镜像中，则合并它们的内容，同时遵守上述规则。


代码样式(使用tab作为indentation)：
```c
void my_func(int arg1) {
	if (arg1 > 114514) {
		exit(1);
	}
}
```

构建32位so可用于srcds时，直接运行make即可，详情可见Makefile。

实际运行srcds进行测试：
启动srcds可通过脚本：/home/rikka/Steam/l4d2/srcds_run
第330行开始指明了调试时LD_PRELOAD的so（我们32位编译结果）、squashfs镜像（HOOKSQFS_FILE）和squashfs镜像映射到的目录前缀（HOOKSQFS_PREFIX）
srcds_run需要以/home/rikka/Steam/l4d2作为工作目录运行

现在我遇到的问题是，如果以`./srcds_run +map c1m1_hotel`启动srcds，则可以发现squashfs镜像提供的l4d2_tanksplayground（文件l4d2_tanksplayground.vpk）这个地图。但是如果以`./srcds_run +map l4d2_tanksplayground`启动srcds，则甚至无法找到游戏本身自带的很多文件，比如c1m1_hotel（由原版l4d2 srcds的某个vpk提供）。看起来像是文件访问乱了或者多线程导致的hook混乱。

为了让事情简化，我提供了测试的squashfs镜像/home/rikka/addons.sqfs，里面只有l4d2_tanksplayground.vpk这一地图。

你的任务是动用各种手段（观察srcds_run输出、在代码中增加你认为有用的调试输出来帮助你锁定问题、添加修改删除源码、使用gdb等等等）来解决这个问题，一直尝试直到srcds_run能以`./srcds_run +map l4d2_tanksplayground`启动，然后游戏不会找不到自带的那些文件，并且要`./srcds_run +map l4d2_tanksplayground`之后能顺利`changelevel c1m1_hotel`才算结束。
