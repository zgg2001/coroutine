It's an asymmetric coroutine library (like lua).

You can use coroutine_open to open a schedule first, and then create coroutine in that schedule. 

You should call coroutine_resume in the thread that you call coroutine_open, and you can't call it in a coroutine in the same schedule.

Coroutines in the same schedule share the stack , so you can create many coroutines without worry about memory.

But switching context will copy the stack the coroutine used.

Read source for detail.

Chinese blog : http://blog.codingnow.com/2012/07/c_coroutine.html

------------------------------------------分割线------------------------------------------

fork云风大佬的协程库进行协程相关的初步学习。主要添加的内容为代码的注释以及个人的一些理解。

csdn blog: https://blog.csdn.net/qq_45698148/article/details/123598446?spm=1001.2014.3001.5501

# 实现分析
## 1. 数据结构
&emsp;&emsp;首先是协程结构体，每个结构体实例对应着一条协程。可以看到其中定义了协程对应的回调函数与其传参，还有核心内存**协程上下文ctx**，以及每个协程对应的**栈区内容**等。
```cpp
struct coroutine {
	coroutine_func func;//协程回调函数
	void *ud;//传参
	ucontext_t ctx;//上下文
	struct schedule * sch;//所属管理者
	ptrdiff_t cap;//已经分配的内存大小
	ptrdiff_t size;//协程运行时栈的保存后大小
	int status;//状态
	char *stack;//栈
};
```
&emsp;&emsp;其次是协程调度器结构体，其中储存了若干协程指针以及当前运行协程id等内容，用于进行协程调度等操作。其中最重要的我觉得是共享栈区**stack**。
```cpp
struct schedule {
	char stack[STACK_SIZE];//栈
	ucontext_t main;//主线程上下文
	int nco;//当前协程数量
	int cap;//协程最大数量
	int running;//当前执行中的协程id
	struct coroutine **co;//协程组
};
```
## 2. 整体思路
&emsp;&emsp;了解了核心数据结构，那么协程的切换是如何实现的呢？我过了一遍源码，大致了解了云风大佬这个协程库的实现原理：

&emsp;&emsp;简单来讲，调度器结构体 `schedule` 中有一块栈上的内存，声明为 `char stack[STACK_SIZE];`，当协程在执行时，会将这块内存当作自己的进程栈来使用。

&emsp;&emsp;当协程执行挂起操作时，会通过 `memcpy` 把执行栈上的内容 `copy` 至协程自己的缓冲区，进行栈区内容的保存；而当协程进行唤醒操作时，只需要把协程缓冲区中的栈内容 `copy` 至执行栈上，即可实现栈区内容的恢复。因为所有的协程在执行中都需要使用这块栈内存，所以这种协程实现方法被称为共享栈。

&emsp;&emsp;除了栈区内容的切换外，也需要进行硬件上下文的切换，对此，Linux存在系统调用来进行上下文的切换与保存，其均定义在头文件 `ucontext.h` 中。此协程库中主要使用的内容有：
* **ucontext_t**：上下文结构体，其中储存了上下文的内容。主要需要关注的有`uc_link: 下一个要执行的上下文`、`uc_stack：此上下文所使用的栈信息`，其余还有信号掩码、硬件上下文等内容。
* **getcontext**：传入一个 `ucontext_t`，初始化它并获取当前上下文环境存入其中。
* **makecontext**：传入一个 `ucontext_t` 和一个函数指针及其参数，作用是指定该上下文的入口函数。即在此上下文被激活后，执行这个被绑定的函数。
* **swapcontext**：传入两个 `ucontext_t`，作用为切换上下文环境。具体操作是保存当前上下文环境至第一个 `ucontext_t`，并激活第二个 `ucontext_t` 的上下文环境。

&emsp;&emsp;所以可以说这个协程库主要就是通过系统调用切换/保存上下文，以及保存每个协程的运行栈来实现的。
&emsp;&emsp;
## 3. 唤醒 resume
&emsp;&emsp;协程的核心操作**唤醒**的实现如下，其中已经加上了我的注释。
```cpp
void 
coroutine_resume(struct schedule * S, int id) {
	assert(S->running == -1);
	assert(id >=0 && id < S->cap);
	struct coroutine *C = S->co[id];//确定唤醒的协程
	if (C == NULL)
		return;
	int status = C->status;
	switch(status) {
	case COROUTINE_READY://全新的协程
		getcontext(&C->ctx);//初始化并获取当前上下文环境：信号掩码/具体硬件上下文
		C->ctx.uc_stack.ss_sp = S->stack;//共享栈
		C->ctx.uc_stack.ss_size = STACK_SIZE;//共享栈大小
		C->ctx.uc_link = &S->main;//当前context执行结束之后要执行的下一个context
		S->running = id;//当前执行协程id
		C->status = COROUTINE_RUNNING;
		uintptr_t ptr = (uintptr_t)S;
		//makecontext修改上下文结构体，设定栈空间为C->ctx->uc_stack，激活后执行mainfunc
		makecontext(&C->ctx, (void (*)(void)) mainfunc, 2, (uint32_t)ptr, (uint32_t)(ptr>>32));
		swapcontext(&S->main, &C->ctx);//保存当前上下文至S->main，并激活C->ctx上下文
		break;
	case COROUTINE_SUSPEND://之前已挂起
		memcpy(S->stack + STACK_SIZE - C->size, C->stack, C->size);//把协程栈的内存copy至调度器中
		S->running = id;
		C->status = COROUTINE_RUNNING;
		swapcontext(&S->main, &C->ctx);//保存当前上下文至S->main，并激活C->ctx上下文
		break;
	default:
		assert(0);
	}
}
```
&emsp;&emsp;在新建一条协程后，其状态为 `COROUTINE_READY`。在上面的源码中，可以看到对于刚创建的协程和被挂起后的协程操作是不一致的。主要原因是因为刚创建的协程并没有初始化上下文，而且也没有指定运行栈为调度器中的共享栈，所以新的协程需要指定上下文相关的内容并绑定入口函数。

&emsp;&emsp;此外，`makecontext` 中的入口函数传参被拆成两部分来传入，我感觉是因为传参类型为 `uint` 而在64位环境下指针大小为8B，为了避免这部分的差异，所以把指针分为前32位和后32位来进行传入。而在随后入口函数的定义中对指针进行拼合操作，来获取正确的指针。

&emsp;&emsp;另外值得一提的是在对已挂起协程的恢复时，需要恢复执行栈，所以使用 `memcpy` 进行内存拷贝。由于栈地址是由高向低发展，所以这里是从后倒着找栈顶 `S->stack + STACK_SIZE - C->size`。

&emsp;&emsp;入口函数 `mainfunc` 定义如下，主要就是传入协程调度器，从而获取正在执行的协程，从而执行对应的回调函数。并在回调函数执行完毕后删除协程，从而宣布此协程执行完毕。
```cpp
static void
mainfunc(uint32_t low32, uint32_t hi32) {
	//上32位和下32位组成一个指针，这里我觉得是为了避免64位与32位系统下long类型大小差异造成的影响
	uintptr_t ptr = (uintptr_t)low32 | ((uintptr_t)hi32 << 32);
	struct schedule *S = (struct schedule *)ptr;
	int id = S->running;
	struct coroutine *C = S->co[id];
	C->func(S,C->ud);//执行协程的回调函数
	_co_delete(C);//执行完毕删除协程
	S->co[id] = NULL;
	--S->nco;
	S->running = -1;
}
```
## 4. 挂起 yield
&emsp;&emsp;协程的核心操作**挂起**的实现如下，其中已经加上了我的注释。
```cpp
void
coroutine_yield(struct schedule * S) {
	int id = S->running;
	assert(id >= 0);
	struct coroutine * C = S->co[id];
	assert((char *)&C > S->stack);
	_save_stack(C,S->stack + STACK_SIZE);//保存协程栈
	C->status = COROUTINE_SUSPEND;
	S->running = -1;
	swapcontext(&C->ctx , &S->main);//保存当前上下文至C->ctx，并激活S->main上下文
}

static void
_save_stack(struct coroutine *C, char *top) {
	char dummy = 0;//获取当前的栈顶地址
	assert(top - &dummy <= STACK_SIZE);
	//此时栈大小就是 top - &dummy
	if (C->cap < top - &dummy) {//协程栈大小不足时，需要重新分配内存，用于保存当前协程栈
		free(C->stack);
		C->cap = top-&dummy;
		C->stack = malloc(C->cap);
	}
	C->size = top - &dummy;
	memcpy(C->stack, &dummy, C->size);//将栈顶至栈底的内存拷贝至C->stack保存
}
```
&emsp;&emsp;挂起操作相对就比较简单了，主要就是**保存执行栈** + **切换上下文**。其中主要的篇幅是在**保存执行栈**这一环节上，主要的逻辑是协程栈内存不足时释放并申请足够的内存，随后 `copy` 执行栈。
&emsp;&emsp;
## 5. 新建与扩容
&emsp;&emsp;值得一提的是协程调度结构体中是通过一个**协程指针数组**来储存所管理的协程的，所以涉及到扩容问题。此协程库中采用了经典的二倍扩容法，通过 `realloc` 进行扩容，具体实现如下：
```cpp
int 
coroutine_new(struct schedule *S, coroutine_func func, void *ud) {
	struct coroutine *co = _co_new(S, func , ud);
	/*
	* 此处主要是判定管理者管理数量是否达到最大
	* 当达到最大时，会使用realloc对协程组空间进行扩容，扩容后大小为两倍当前大小（经典的两倍扩容），随后插入在最后端
	* 没达到最大时，则遍历协程组找到空闲位置插入
	*/
	if (S->nco >= S->cap) {//扩容后，将新的协程插入到扩容后的第一个空闲处
		int id = S->cap;
		S->co = realloc(S->co, S->cap * 2 * sizeof(struct coroutine *));
		memset(S->co + S->cap , 0 , sizeof(struct coroutine *) * S->cap);
		S->co[S->cap] = co;
		S->cap *= 2;
		++S->nco;
		return id;
	} else {//遍历整个协程组找到位置插入新协程
		int i;
		for (i=0;i<S->cap;i++) {
			int id = (i+S->nco) % S->cap;
			if (S->co[id] == NULL) {
				S->co[id] = co;
				++S->nco;
				return id;
			}
		}
	}
	assert(0);
	return -1;
}
```