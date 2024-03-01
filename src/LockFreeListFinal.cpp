
#include "LockFreeListFinal.h"

#include "sync_win32.h"

namespace lockfree {

#define EPSILON 0.2
#define setMark(address) ((node_lf *)((uintptr_t)address | 2))
#define setFlag(address) ((node_lf *)((uintptr_t)address | 1))
#define getMark(address) ((int)((uintptr_t)address & 2) == 2 ?1:0)
#define getFlag(address) ((int)((uintptr_t)address & 1))
#define getNodeAddress(address) ((node_lf *)((uintptr_t)address & -4))

	node_lf* constructArgs(node_lf* node, int mark, int flag)
	{
		node_lf* temp = getNodeAddress(node);
		if (mark == 1)
			(temp) = setMark(temp);
		if (flag == 1)
			(temp) = setFlag(temp);
		return temp;
	}

	void HelpMarked(node_lf* prev, node_lf* del_node)
	{
		node_lf* next = getNodeAddress(del_node)->next;

		node_lf* del = constructArgs(del_node, 0, 1);
		node_lf* next_node = constructArgs(next, 0, 0);
		__sync_val_compare_and_swap((unsigned long long*) & (getNodeAddress(prev)->next),
			(unsigned long long)del, (unsigned long long)next_node);

		free(getNodeAddress(del_node));

	}

	return_sf SearchFrom(int k, node_lf* curr)
	{

		return_sf  s;
		node_lf* next = curr->next;
		while (getNodeAddress(next)->key <= k)
		{
			while (getMark(next) == 1 && (getMark(curr) == 0 || getNodeAddress(curr)->next != getNodeAddress(next)))
			{
				if (getNodeAddress(curr)->next == getNodeAddress(next))
				{
					HelpMarked(curr, next);
				}
				next = getNodeAddress(curr)->next;
			}
			if (getNodeAddress(next)->key <= k)
			{
				curr = next;
				next = getNodeAddress(curr)->next;
			}

		}
		s.current = curr;
		s.next = next;
		return s;
	}

	void TryMark(node_lf* del_node)
	{
		node_lf* next;
		node_lf* result;
		do
		{
			next = getNodeAddress(del_node)->next;

			node_lf* next_node = constructArgs(next, 0, 0);
			node_lf* next_node1 = constructArgs(next, 1, 0);
			result = (node_lf*)__sync_val_compare_and_swap((unsigned long long*) & (getNodeAddress(del_node)->next),
				(unsigned long long)(next_node), (unsigned long long)(next_node1));

			if (getMark(result) == 0 && getFlag(result) == 1)
				HelpFlagged(del_node, result);
		} while (getMark(getNodeAddress(del_node)->next) != 1);
	}

	void HelpFlagged(node_lf* prev, node_lf* del_node)
	{
		getNodeAddress(del_node)->backlink = prev;
		if (getMark(getNodeAddress(del_node)->next) == 0)
			TryMark(del_node);
		HelpMarked(prev, del_node);
	}

	int insert(int k, node_lf* head, void* value)
	{
		return_sf s = SearchFrom(k, head);
		node_lf* prev;
		node_lf* next;
		node_lf* prev_succ;
		prev = s.current;
		next = s.next;
		if (getNodeAddress(prev)->key == k)
			return -1;
		node_lf* newNode = (node_lf*)malloc(sizeof(node_lf));
		newNode->key = k;
		newNode->value = value;   //added to implement the key:value pair.

		while (1)
		{
			prev_succ = getNodeAddress(prev)->next;
			if (getFlag(prev_succ) == 1)
				HelpFlagged(prev, getNodeAddress(prev_succ)->next);
			else
			{
				newNode->next = next;
				node_lf* next_node = constructArgs(next, 0, 0);
				node_lf* new_Node = constructArgs(newNode, 0, 0);
				node_lf* result;
				result = (node_lf*)__sync_val_compare_and_swap((unsigned long long*) & (getNodeAddress(prev)->next),
					(unsigned long long)(next_node), (unsigned long long)(new_Node));

				if (result == next)
				{
					return 0;
				}
				else
				{
					if (getFlag(result) == 1)
						HelpFlagged(prev, (result)->next);
					while (getMark(prev) == 1)
						prev = getNodeAddress(prev)->backlink;
				}

			}
			s = SearchFrom(k, prev);
			prev = s.current;
			next = s.next;
			if (getNodeAddress(s.current)->key == k)
			{
				free(newNode);
				return -1;
			}
		}
		return 0;
	}

	return_tf TryFlag(node_lf* prev, node_lf* target)
	{
		return_tf r;
		while (1)
		{
			if (getNodeAddress(prev)->next == target && getMark(getNodeAddress(prev)->next) == 0 && getFlag(getNodeAddress(prev)->next) == 1) // Already Flagged. Some other process would delete_node it.
			{
				r.node = prev;
				r.result = 0;
				r.del_node = NULL;
				return r;
			}
			node_lf* target_node = constructArgs(target, 0, 0);
			node_lf* target_node_new = constructArgs(target, 0, 1);
			node_lf* result;
			result = (node_lf*)__sync_val_compare_and_swap((unsigned long long*) & (getNodeAddress(prev)->next),
				(unsigned long long)(target_node), (unsigned long long)(target_node_new));



			if (result == target && getMark(result) == 0 && getFlag(result) == 0)
			{
				r.node = prev;
				r.result = 1;
				r.del_node = NULL;
				return r;
			}
			if (result == target && getMark(result) == 0 && getFlag(result) == 1)
			{
				r.node = prev;
				r.result = 0;
				r.del_node = NULL;
				return r;
			}
			while (getMark(getNodeAddress(prev)->next) == 1)
				prev = getNodeAddress(prev)->backlink;


			return_sf s = SearchFrom((getNodeAddress(target)->key) - EPSILON, prev);
			r.node = s.current;
			r.prev_node = s.current;
			r.del_node = s.next;
			prev = s.current;
			if (s.next != target)
			{
				r.node = NULL;
				r.result = 0;
				return r;
			}
		}
	}

	node_lf* Search(int key, node_lf* head) {
		return_sf s = SearchFrom(key, head);
		if (s.current->key == key) {
			return s.current;
		}
		else
			return NULL;
	}

	int delete_node(int k, node_lf* head) // returns 0 if delete is successful, -1 on failure.
	{
#ifdef debug
		printf("Delete start\n");
#endif
		return_sf s = SearchFrom(k - EPSILON, head);
		node_lf* prev = s.current;
		node_lf* del = s.next;
		if (getNodeAddress(del)->key != k)
			return -1;
		return_tf tf = TryFlag(prev, del);
		prev = tf.node;
		if (tf.del_node != NULL)
			del = tf.del_node;
		if (prev != NULL)
			HelpFlagged(prev, del);
		if (tf.result == 0)
		{
			return -1;
		}
#ifdef debug
		printf("Delete sucess %d\n", k);
#endif
		return 0;
	}

	node_lf* init_LF_list()
	{
		node_lf* head = (node_lf*)malloc(sizeof(node_lf));
		node_lf* tail = (node_lf*)malloc(sizeof(node_lf));
		head->next = tail;
		head->key = std::numeric_limits<int>::min();
		head->value = NULL;
		tail->next = NULL;
		tail->key = std::numeric_limits<int>::max();
		tail->value = NULL;
		return head;
	}

	void printlist(node_lf* head)
	{
		head = getNodeAddress(head)->next;
		while (getNodeAddress(head)->key != std::numeric_limits<int>::max())
		{
			printf("%d\t", getNodeAddress(head)->key);
			head = head->next;
		}
		printf("\n");
	}

	void destroy(node_lf* head)
	{
		node_lf* next;
		node_lf* curr = getNodeAddress(head);
		while (getNodeAddress(curr) != 0)
		{
			next = getNodeAddress(curr)->next;
			free(curr);
			curr = next;
		}
	}

	List list_init()
	{
		List temp = (List)malloc(sizeof(struct list));
		temp->head = init_LF_list();
		temp->delete_node = delete_node;
		temp->insert = insert;
		temp->search = Search;
		temp->print = printlist;
		temp->destructor = destroy;
		return temp;
	}
}




