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
	#include <ucontext.h>//linux��������ص�ͷ�ļ�
#endif 

#define STACK_SIZE (1024*1024)//��ʼջ��Ϊ1M
#define DEFAULT_COROUTINE 16

struct coroutine;

/*
* �˽ṹ��ΪЭ�̹����ߣ�����������
* ���ڹ������е�Э��
*/
struct schedule {
	char stack[STACK_SIZE];//ջ
	ucontext_t main;//���߳�������
	int nco;//��ǰЭ������
	int cap;//Э���������
	int running;//��ǰִ���е�Э��id
	struct coroutine **co;//Э����
};

/*
* �˽ṹ��ΪЭ�̽ṹ��
* ���ڴ��浥��Э����ص�����
*/
struct coroutine {
	coroutine_func func;//Э�̻ص�����
	void *ud;//����
	ucontext_t ctx;//������
	struct schedule * sch;//����������
	ptrdiff_t cap;//�Ѿ�������ڴ��С
	ptrdiff_t size;//Э������ʱջ�ı�����С
	int status;//״̬
	char *stack;//ջ
};

/*
* �ڲ�_�½�����ʼ��Э��
* ����ֵΪһ��Э�̵�ָ��
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
* �ڲ�_ɾ��Э��
*/
void
_co_delete(struct coroutine *co) {
	free(co->stack);
	free(co);
}

/*
* �½�Э�̹�����
* ����ֵΪһ�������ߵ�ָ��
*/
struct schedule * 
coroutine_open(void) {
	struct schedule *S = malloc(sizeof(*S));
	S->nco = 0;
	S->cap = DEFAULT_COROUTINE;
	S->running = -1;
	S->co = malloc(sizeof(struct coroutine *) * S->cap);//Э����ռ�����
	memset(S->co, 0, sizeof(struct coroutine *) * S->cap);
	return S;
}

/*
* �رչ�����
* ������Э���鰤��ɾ��Э��
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
* �½�Э��
* ����ֵΪ��Э�̵�id
*/
int 
coroutine_new(struct schedule *S, coroutine_func func, void *ud) {
	struct coroutine *co = _co_new(S, func , ud);
	/*
	* �˴���Ҫ���ж������߹��������Ƿ�ﵽ���
	* ���ﵽ���ʱ����ʹ��realloc��Э����ռ�������ݣ����ݺ��СΪ������ǰ��С��������������ݣ���������������
	* û�ﵽ���ʱ�������Э�����ҵ�����λ�ò���
	*/
	if (S->nco >= S->cap) {//���ݺ󣬽��µ�Э�̲��뵽���ݺ�ĵ�һ�����д�
		int id = S->cap;
		S->co = realloc(S->co, S->cap * 2 * sizeof(struct coroutine *));
		memset(S->co + S->cap , 0 , sizeof(struct coroutine *) * S->cap);
		S->co[S->cap] = co;
		S->cap *= 2;
		++S->nco;
		return id;
	} else {//��������Э�����ҵ�λ�ò�����Э��
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
	//��32λ����32λ���һ��ָ�룬�����Ҿ�����Ϊ�˱���64λ��32λϵͳ��long���ʹ�С������ɵ�Ӱ��
	uintptr_t ptr = (uintptr_t)low32 | ((uintptr_t)hi32 << 32);
	struct schedule *S = (struct schedule *)ptr;
	int id = S->running;
	struct coroutine *C = S->co[id];
	C->func(S,C->ud);//ִ��Э�̵Ļص�����
	_co_delete(C);//ִ�����ɾ��Э��
	S->co[id] = NULL;
	--S->nco;
	S->running = -1;
}

/*
* Э�̵Ļ��Ѳ���
*/
void 
coroutine_resume(struct schedule * S, int id) {
	assert(S->running == -1);
	assert(id >=0 && id < S->cap);
	struct coroutine *C = S->co[id];//ȷ�����ѵ�Э��
	if (C == NULL)
		return;
	int status = C->status;
	switch(status) {
	case COROUTINE_READY://ȫ�µ�Э��
		getcontext(&C->ctx);//��ʼ������ȡ��ǰ�����Ļ������ź�����/����Ӳ��������
		C->ctx.uc_stack.ss_sp = S->stack;//����ջ
		C->ctx.uc_stack.ss_size = STACK_SIZE;//����ջ��С
		C->ctx.uc_link = &S->main;//��ǰcontextִ�н���֮��Ҫִ�е���һ��context
		S->running = id;//��ǰִ��Э��id
		C->status = COROUTINE_RUNNING;
		uintptr_t ptr = (uintptr_t)S;
		//makecontext�޸������Ľṹ�壬�趨ջ�ռ�ΪC->ctx->uc_stack�������ִ��mainfunc
		makecontext(&C->ctx, (void (*)(void)) mainfunc, 2, (uint32_t)ptr, (uint32_t)(ptr>>32));
		swapcontext(&S->main, &C->ctx);//���浱ǰ��������S->main��������C->ctx������
		break;
	case COROUTINE_SUSPEND://֮ǰ�ѹ���
		memcpy(S->stack + STACK_SIZE - C->size, C->stack, C->size);//��Э��ջ���ڴ�copy����������
		S->running = id;
		C->status = COROUTINE_RUNNING;
		swapcontext(&S->main, &C->ctx);//���浱ǰ��������S->main��������C->ctx������
		break;
	default:
		assert(0);
	}
}

static void
_save_stack(struct coroutine *C, char *top) {
	char dummy = 0;//��ȡ��ǰ��ջ����ַ
	assert(top - &dummy <= STACK_SIZE);
	//��ʱջ��С���� top - &dummy
	if (C->cap < top - &dummy) {//Э��ջ��С����ʱ����Ҫ���·����ڴ棬���ڱ��浱ǰЭ��ջ
		free(C->stack);
		C->cap = top-&dummy;
		C->stack = malloc(C->cap);
	}
	C->size = top - &dummy;
	memcpy(C->stack, &dummy, C->size);//��ջ����ջ�׵��ڴ濽����C->stack����
}

/*
* Э�̵Ĺ������
*/
void
coroutine_yield(struct schedule * S) {
	int id = S->running;
	assert(id >= 0);
	struct coroutine * C = S->co[id];
	assert((char *)&C > S->stack);
	_save_stack(C,S->stack + STACK_SIZE);//����Э��ջ
	C->status = COROUTINE_SUSPEND;
	S->running = -1;
	swapcontext(&C->ctx , &S->main);//���浱ǰ��������C->ctx��������S->main������
}

/*
* ��ȡָ��Э�̵�״̬
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
* ��ȡ�����е�Э��id
*/
int 
coroutine_running(struct schedule * S) {
	return S->running;
}

