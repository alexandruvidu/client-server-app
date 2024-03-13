#ifndef QUEUE_H_
#define QUEUE_H_

#include <stdio.h>
#include <stdlib.h>

typedef struct QueueNode{
	Item elem;
	struct QueueNode *next;
}QueueNode;

typedef struct Queue{
	QueueNode *front;
	QueueNode *rear;
	long size;
}Queue;

Queue* createQueue(void){
	Queue* queue = (Queue*) malloc(sizeof(Queue));
	queue->size = 0;
	queue->front = NULL;
	queue->rear = NULL;
	return queue;
} 

int isQueueEmpty(Queue *q){
	return !q->front;
}

void enqueue(Queue *q, Item elem){
	QueueNode* node = (QueueNode*) malloc(sizeof(QueueNode));
	node->elem = elem;
	node->next = NULL;
	if(!q->front){
		q->front = node;
		q->rear = node;
	}
	else {
		q->rear->next = node;
		q->rear = node;
	}
	q->size++;
}

Item front(Queue* q){
	return q->front->elem;
}

void dequeue(Queue* q){
	QueueNode* node = q->front;
	q->front = q->front->next;
	q->size--;
	if(q->size == 1)
		q->rear = q->front;
	else if(!q->size)
		q->front = q->rear = NULL;
	free(node);
}

void destroyQueue(Queue *q){
	while(q->front)
		dequeue(q);
	free(q);
}

#endif
