#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include <iostream>
#include <limits>

namespace lockfree {

	typedef struct node
	{
		int key;
		void* value;
		struct node* next;
		struct node* backlink;
	}node_lf;

	struct list
	{
		node_lf* head;
		node_lf* (*search)(int key, node_lf* head);
		int (*insert)(int k, node_lf* head, void* value);
		int (*delete_node)(int, node_lf*);
		void (*print)(node_lf*);
		void (*destructor)(node_lf*);
	};
	typedef struct list* List;
	typedef struct searchfrom
	{
		node_lf* current;
		node_lf* next;
	}return_sf;

	typedef struct csArg
	{
		int* node;
	}cs_arg;

	typedef struct return_tryFlag
	{
		node_lf* node;
		int result;
		node_lf* prev_node;
		node_lf* del_node;
	}return_tf;

	void HelpFlagged(node_lf* prev, node_lf* del_node);
	node_lf* constructArgs(node_lf* node, int mark, int flag);
	void HelpMarked(node_lf* prev, node_lf* del_node);
	return_sf SearchFrom(int k, node_lf* curr);
	void TryMark(node_lf* del_node);
	void HelpFlagged(node_lf* prev, node_lf* del_node);
	int insert(int k, node_lf* head, void* value);
	node_lf* Search(int key, node_lf* head);
	return_tf TryFlag(node_lf* prev, node_lf* target);
	int delete_node(int k, node_lf* head);
	void printlist(node_lf* head);
	void destroy(node_lf* head);
	List list_init();
}
