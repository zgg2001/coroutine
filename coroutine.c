#include "coroutine.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stddef.h>
#include <string.h>
#include <stdint.h>

#if __APPLE__ && __MACH__
	#include <sys/ucontext.h>
#else 
	#include <ucontext.h>//linux上下文相关的头文件
#endif 

#define STACK_SIZE (1024*1024)//初始栈设为1M
#define DEFAULT_COROUTINE 16

struct coroutine;

/*
* 此结构体为协程管理者（调度器？）
* 用于管理所有的协程
*/
struct schedule {
	char stack[STACK_SIZE];//栈
	ucontext_t main;//主线程上下文
	int nco;//当前协程数量
	int cap;//协程最大数量
	int running;//当前执行中的协程id
	struct coroutine **co;//协程组
};

/*
* 此结构体为协程结构体
* 用于储存单个协程相关的内容
*/
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

/*
* 内部_新建并初始化协程
* 返回值为一个协程的指针
*/
struct coroutine * 
_co_new(struct schedule *S , coroutine_func func, void *ud) {
	struct coroutine * co = malloc(sizeof(*co));
	co->func = func;
	co->ud = ud;
	co->sch = S;
	co->cap = 0;
	co->size = 0;
	co->status = COROUTINE_READY;
	co->stack = NULL;
	return co;
}

/*
* 内部_删除协程
*/
void
_co_delete(struct coroutine *co) {
	free(co->stack);
	free(co);
}

/*
* 新建协程管理者
* 返回值为一个管理者的指针
*/
struct schedule * 
coroutine_open(void) {
	struct schedule *S = malloc(sizeof(*S));
	S->nco = 0;
	S->cap = DEFAULT_COROUTINE;
	S->running = -1;
	S->co = malloc(sizeof(struct coroutine *) * S->cap);//协程组空间申请
	memset(S->co, 0, sizeof(struct coroutine *) * S->cap);
	return S;
}

/*
* 关闭管理者
* 即遍历协程组挨个删除协程
*/
void 
coroutine_close(struct schedule *S) {
	int i;
	for (i=0;i<S->cap;i++) {
		struct coroutine * co = S->co[i];
		if (co) {
			_co_delete(co);
		}
	}
	free(S->co);
	S->co = NULL;
	free(S);
}

/*
* 新建协程
* 返回值为新协程的id
*/
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

/*
* 协程的唤醒操作
*/
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

/*
* 协程的挂起操作
*/
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

/*
* 获取指定协程的状态
*/
int 
coroutine_status(struct schedule * S, int id) {
	assert(id>=0 && id < S->cap);
	if (S->co[id] == NULL) {
		return COROUTINE_DEAD;
	}
	return S->co[id]->status;
}

/*
* 获取运行中的协程id
*/
int 
coroutine_running(struct schedule * S) {
	return S->running;
}

