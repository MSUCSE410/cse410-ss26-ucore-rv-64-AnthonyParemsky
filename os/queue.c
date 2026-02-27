#include "queue.h"
#include "defs.h"

void init_queue(struct queue *q)
{
	q->front = q->tail = 0;
	q->empty = 1;
}

void push_queue(struct queue *q, int value, int stride)
{
	if (!q->empty && q->front == q->tail) {
		panic("queue shouldn't be overflow");
	}
	q->empty = 0;
	q->tail = (q->tail + 1) % NPROC;
	//Put the item in the right spot, for prio
	int i = q->tail-1;
	if(i < 0)
	{
		i += NPROC;
	}
	int i_plus_1 = q->tail;
	for(;i != q->front;)
	{
		//Wrap around
		//Debug
		//printf("\ni = %d q->front = %d\n\n",i,q->front);
		uint64 curr_v = q->data[i];
		int curr_prio = (curr_v >> 32) & 0xffffffff;
		if(curr_prio > stride)
		{
			q->data[i_plus_1] = curr_v;
		}
		else
		{
			//Found where to put incoming val
			break;
		}

		i--;
		if(i < 0)
		{
			i += NPROC;
		}
		i_plus_1 = i + 1;
	}
	q->data[i] = ((uint64)stride << 32) | value;
	q->data[q->tail] = value;
}

int pop_queue(struct queue *q)
{

	/*debug
	printf("\nREADING QUEUE\n");
	int i;
	for(i = q->front;i != q->tail;i++)
	{
		i %= NPROC;
		int prio = q->data[i] >> 32;
		//debug
		if(prio != 0)
		{
			printf("Queue at i = %d: %d\n",i,prio);
		}
	}
	printf("DONE READING QUEUE\n\n");
	//debug*/

	if (q->empty)
		return -1;
	int value = q->data[q->front];
	q->front = (q->front + 1) % NPROC;
	if (q->front == q->tail)
		q->empty = 1;
	return value;
}
