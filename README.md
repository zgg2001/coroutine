It's an asymmetric coroutine library (like lua).

You can use coroutine_open to open a schedule first, and then create coroutine in that schedule. 

You should call coroutine_resume in the thread that you call coroutine_open, and you can't call it in a coroutine in the same schedule.

Coroutines in the same schedule share the stack , so you can create many coroutines without worry about memory.

But switching context will copy the stack the coroutine used.

Read source for detail.

Chinese blog : http://blog.codingnow.com/2012/07/c_coroutine.html

------------------------------------------�ָ���------------------------------------------

fork�Ʒ���е�Э�̿����Э����صĳ���ѧϰ����Ҫ��ӵ�����Ϊ�����ע���Լ����˵�һЩ��⡣

csdn blog: https://blog.csdn.net/qq_45698148/article/details/123598446?spm=1001.2014.3001.5501

# ʵ�ַ���
## 1. ���ݽṹ
&emsp;&emsp;������Э�̽ṹ�壬ÿ���ṹ��ʵ����Ӧ��һ��Э�̡����Կ������ж�����Э�̶�Ӧ�Ļص��������䴫�Σ����к����ڴ�**Э��������ctx**���Լ�ÿ��Э�̶�Ӧ��**ջ������**�ȡ�
```cpp
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
```
&emsp;&emsp;�����Э�̵������ṹ�壬���д���������Э��ָ���Լ���ǰ����Э��id�����ݣ����ڽ���Э�̵��ȵȲ�������������Ҫ���Ҿ����ǹ���ջ��**stack**��
```cpp
struct schedule {
	char stack[STACK_SIZE];//ջ
	ucontext_t main;//���߳�������
	int nco;//��ǰЭ������
	int cap;//Э���������
	int running;//��ǰִ���е�Э��id
	struct coroutine **co;//Э����
};
```
## 2. ����˼·
&emsp;&emsp;�˽��˺������ݽṹ����ôЭ�̵��л������ʵ�ֵ��أ��ҹ���һ��Դ�룬�����˽����Ʒ�������Э�̿��ʵ��ԭ��

&emsp;&emsp;���������������ṹ�� `schedule` ����һ��ջ�ϵ��ڴ棬����Ϊ `char stack[STACK_SIZE];`����Э����ִ��ʱ���Ὣ����ڴ浱���Լ��Ľ���ջ��ʹ�á�

&emsp;&emsp;��Э��ִ�й������ʱ����ͨ�� `memcpy` ��ִ��ջ�ϵ����� `copy` ��Э���Լ��Ļ�����������ջ�����ݵı��棻����Э�̽��л��Ѳ���ʱ��ֻ��Ҫ��Э�̻������е�ջ���� `copy` ��ִ��ջ�ϣ�����ʵ��ջ�����ݵĻָ�����Ϊ���е�Э����ִ���ж���Ҫʹ�����ջ�ڴ棬��������Э��ʵ�ַ�������Ϊ����ջ��

&emsp;&emsp;����ջ�����ݵ��л��⣬Ҳ��Ҫ����Ӳ�������ĵ��л����Դˣ�Linux����ϵͳ���������������ĵ��л��뱣�棬���������ͷ�ļ� `ucontext.h` �С���Э�̿�����Ҫʹ�õ������У�
* **ucontext_t**�������Ľṹ�壬���д����������ĵ����ݡ���Ҫ��Ҫ��ע����`uc_link: ��һ��Ҫִ�е�������`��`uc_stack������������ʹ�õ�ջ��Ϣ`�����໹���ź����롢Ӳ�������ĵ����ݡ�
* **getcontext**������һ�� `ucontext_t`����ʼ��������ȡ��ǰ�����Ļ����������С�
* **makecontext**������һ�� `ucontext_t` ��һ������ָ�뼰�������������ָ���������ĵ���ں��������ڴ������ı������ִ��������󶨵ĺ�����
* **swapcontext**���������� `ucontext_t`������Ϊ�л������Ļ�������������Ǳ��浱ǰ�����Ļ�������һ�� `ucontext_t`��������ڶ��� `ucontext_t` �������Ļ�����

&emsp;&emsp;���Կ���˵���Э�̿���Ҫ����ͨ��ϵͳ�����л�/���������ģ��Լ�����ÿ��Э�̵�����ջ��ʵ�ֵġ�
&emsp;&emsp;
## 3. ���� resume
&emsp;&emsp;Э�̵ĺ��Ĳ���**����**��ʵ�����£������Ѿ��������ҵ�ע�͡�
```cpp
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
```
&emsp;&emsp;���½�һ��Э�̺���״̬Ϊ `COROUTINE_READY`���������Դ���У����Կ������ڸմ�����Э�̺ͱ�������Э�̲����ǲ�һ�µġ���Ҫԭ������Ϊ�մ�����Э�̲�û�г�ʼ�������ģ�����Ҳû��ָ������ջΪ�������еĹ���ջ�������µ�Э����Ҫָ����������ص����ݲ�����ں�����

&emsp;&emsp;���⣬`makecontext` �е���ں������α���������������룬�Ҹо�����Ϊ��������Ϊ `uint` ����64λ������ָ���СΪ8B��Ϊ�˱����ⲿ�ֵĲ��죬���԰�ָ���Ϊǰ32λ�ͺ�32λ�����д��롣���������ں����Ķ����ж�ָ�����ƴ�ϲ���������ȡ��ȷ��ָ�롣

&emsp;&emsp;����ֵ��һ������ڶ��ѹ���Э�̵Ļָ�ʱ����Ҫ�ָ�ִ��ջ������ʹ�� `memcpy` �����ڴ濽��������ջ��ַ���ɸ���ͷ�չ�����������ǴӺ�����ջ�� `S->stack + STACK_SIZE - C->size`��

&emsp;&emsp;��ں��� `mainfunc` �������£���Ҫ���Ǵ���Э�̵��������Ӷ���ȡ����ִ�е�Э�̣��Ӷ�ִ�ж�Ӧ�Ļص����������ڻص�����ִ����Ϻ�ɾ��Э�̣��Ӷ�������Э��ִ����ϡ�
```cpp
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
```
## 4. ���� yield
&emsp;&emsp;Э�̵ĺ��Ĳ���**����**��ʵ�����£������Ѿ��������ҵ�ע�͡�
```cpp
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
```
&emsp;&emsp;���������ԾͱȽϼ��ˣ���Ҫ����**����ִ��ջ** + **�л�������**��������Ҫ��ƪ������**����ִ��ջ**��һ�����ϣ���Ҫ���߼���Э��ջ�ڴ治��ʱ�ͷŲ������㹻���ڴ棬��� `copy` ִ��ջ��
&emsp;&emsp;
## 5. �½�������
&emsp;&emsp;ֵ��һ�����Э�̵��Ƚṹ������ͨ��һ��**Э��ָ������**�������������Э�̵ģ������漰���������⡣��Э�̿��в����˾���Ķ������ݷ���ͨ�� `realloc` �������ݣ�����ʵ�����£�
```cpp
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
```