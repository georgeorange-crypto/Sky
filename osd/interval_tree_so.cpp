#include <iostream>
#include <iomanip>
#include <string>
#include <vector>  // 新增：用于存储重叠区间结点
#include <pthread.h>
#include <unistd.h>

using namespace std;

#define BLACK 0
#define RED 1

// 区间结构体
typedef struct interval {
    long int low;
    long int high;
    long int obj_id;
    long int  data_low;
    long int data_high;
    long int comp_low;
    long int  comp_high;
    int      flag; // 0: 保持原始数据 ； 1,2... 根据算法进行数据处理
} interval;

// 区间树结点
typedef struct IntervalTNode {
    long int key;                  // 用区间的 low 值作为 key
    bool color;               // 红黑树颜色
    IntervalTNode* parent;    // 父节点
    IntervalTNode* left;      // 左孩子
    IntervalTNode* right;     // 右孩子

    interval inte;            // 存储的区间信息
    long int  max;                  // 以该结点为根的子树中所有区间 high 的最大值
}IntervalTNode;

// 区间树结构体
typedef struct IntervalTree {
    IntervalTNode* root;
    struct IntervalTNode* croot;
    pthread_rwlock_t Tree_rwlock;
} IntervalTree;

// 定义哨兵结点 NIL
interval interval0 = { (long int)-1, (long int )-1, (long int )-1,(long int )-1, (long int )-1, (long int)-1, (long int)-1 };
IntervalTNode NILL_NODE = { (long int)-1, BLACK, nullptr, nullptr, nullptr, interval0,(long int) -1 };
IntervalTNode* NIL = &NILL_NODE;

void IntervalT_Delete(IntervalTree* T, IntervalTNode* z);

void cp_tnode(IntervalTNode* dest, IntervalTNode* src)
{
	dest->key = src->key;
	dest->color = src->color;
	dest->parent = src->parent;
	dest->left = src->left;
	dest->right = src->right;
	dest->max = src->max;

	dest->inte.obj_id = src->inte.obj_id; 
	dest->inte.low = src->inte.low; 
	dest->inte.high = src->inte.high; 
	dest->inte.data_low = src->inte.data_low; 
	dest->inte.data_high = src->inte.data_high; 
	dest->inte.comp_low = src->inte.comp_low; 
	dest->inte.comp_high = src->inte.comp_high; 

}


// 返回三个整数中的最大值
int Max(int a, int b, int c) {
    int m = (a > b ? a : b);
    return (m > c ? m : c);
}

// 判断两个区间是否重叠
bool Overlap(interval a, interval b) {
    return !(a.high < b.low || a.low > b.high);
}

bool can_merge(IntervalTNode * dest, IntervalTNode * src){
	// TODO : check value to 
	return false;
}
// added by mayl
int  Merge(IntervalTNode * dest,  IntervalTNode * src) {
	int ret = 0;
	if(Overlap(dest->inte, src->inte) && can_merge(dest, src)){	
		int high = (dest->inte.high >= src->inte.high ? dest->inte.high : src->inte.high); 
		int low = (dest->inte.low <= src->inte.low ? dest->inte.low : src->inte.low);
		dest->inte.high = high;
		dest->inte.low = low;
		ret = 1;
		// TODO: merge value

	}
	return ret;
}

// 修改部分：新增递归辅助函数，用于搜索所有与区间 i 重叠的结点
void IntervalT_SearchAll_Helper(IntervalTNode* node, interval i, vector<IntervalTNode*>& result) {
    if (node == NIL)
        return;
    // 若左子树可能存在重叠结点，则递归搜索左子树
    if (node->left != NIL && node->left->max >= i.low)
        IntervalT_SearchAll_Helper(node->left, i, result);
    // 检查当前结点是否重叠
    if (Overlap(i, node->inte))
        result.push_back(node);
    // 若当前结点的 key 小于等于查询区间的 high，则右子树可能存在重叠结点，递归搜索右子树
    if (node->right != NIL && node->key <= i.high)
        IntervalT_SearchAll_Helper(node->right, i, result);
}

// 修改部分：新增接口，返回所有与区间 i 重叠的结点集合
vector<IntervalTNode*> IntervalT_SearchAll(IntervalTree* T, interval i) {
    vector<IntervalTNode*> result;
    IntervalT_SearchAll_Helper(T->root, i, result);
    return result;
}

// 仅返回第一个与给定区间有重叠的结点（原有函数）
IntervalTNode* IntervalT_Search(IntervalTree* T, interval i) {
    IntervalTNode* x = T->root;
    while (x != NIL && !Overlap(i, x->inte)) {
        if (x->left != NIL && x->left->max >= i.low)
            x = x->left;
        else
            x = x->right;
    }
    return x;
}

// 精确查找：只有当区间的 low 和 high 都完全匹配时才返回结点
IntervalTNode* IntervalT_SearchExact(IntervalTree* T, interval i) {
    	
    //fprintf(stderr, "start exact search , tree %p \n", T);
    //fprintf(stderr, "start exact search , root %p \n", T->root);
    IntervalTNode* x = T->root;
    //fprintf(stderr, "search exact node , T %p, root %p , x %p NIL %p \n", T, T->root, x, NIL);
    while (x != NIL) {
    //while (x->key != -1 ) {
        if (x->inte.low == i.low && x->inte.high == i.high)
            return x;
        if (i.low < x->key)
            x = x->left;
        else
            x = x->right;
    }
    return NIL;
}

// 中序遍历区间树并打印每个结点的信息
void IntervalT_InorderWalk(IntervalTNode* x) {
    if (x != NIL) {
        IntervalT_InorderWalk(x->left);
        cout << "[" << setw(3) << x->inte.low << ", " << setw(3) << x->inte.high << "] ";
        cout << (x->color == RED ? "Red " : "Black ");
        cout << "Max:" << x->max << endl;
        IntervalT_InorderWalk(x->right);
    }
}

// 使用类似 "├──"、"└──" 的符号打印树结构，直观区分左右子树
void printBT(const string& prefix, const IntervalTNode* node, bool isLeft) {
    if (node == NIL) {
        cout << prefix;
        cout << (isLeft ? "├──" : "└──");
        cout << "NIL" << endl;
        return;
    }
    cout << prefix;
    cout << (isLeft ? "├──" : "└──");
    cout << (node->color == RED ? "R" : "B");
    cout << "[" << node->inte.low << "," << node->inte.high << "](max:" << node->max << ")" << endl;
    printBT(prefix + (isLeft ? "│   " : "    "), node->left, true);
    printBT(prefix + (isLeft ? "│   " : "    "), node->right, false);
}

// 返回以 x 为根的子树中最小的结点
IntervalTNode* IntervalT_Minimum(IntervalTNode* x) {
    while (x->left != NIL)
        x = x->left;
    return x;
}

// 返回结点 x 的后继结点
IntervalTNode* IntervalT_Successor(IntervalTNode* x) {
    if (x->right != NIL)
        return IntervalT_Minimum(x->right);
    IntervalTNode* y = x->parent;
    while (y != NIL && x == y->right) {
        x = y;
        y = y->parent;
    }
    return y;
}

// 左旋操作
void Left_Rotate(IntervalTree* T, IntervalTNode* x) {
    IntervalTNode* y = x->right;
    x->right = y->left;
    if (y->left != NIL)
        y->left->parent = x;
    y->parent = x->parent;
    if (x->parent == NIL)
        T->root = y;
    else if (x == x->parent->left)
        x->parent->left = y;
    else
        x->parent->right = y;
    y->left = x;
    x->parent = y;

    // 更新 max 值
    x->max = Max(x->inte.high, x->left->max, x->right->max);
    y->max = Max(y->inte.high, x->max, y->right->max);
}

// 右旋操作
void Right_Rotate(IntervalTree* T, IntervalTNode* x) {
    IntervalTNode* y = x->left;
    x->left = y->right;
    if (y->right != NIL)
        y->right->parent = x;
    y->parent = x->parent;
    if (x->parent == NIL)
        T->root = y;
    else if (x == x->parent->left)
        x->parent->left = y;
    else
        x->parent->right = y;
    y->right = x;
    x->parent = y;

    // 更新 max 值
    x->max = Max(x->inte.high, x->left->max, x->right->max);
    y->max = Max(y->inte.high, y->left->max, x->max);
}

// 插入修正：维护红黑树性质
void IntervalT_InsertFixup(IntervalTree* T, IntervalTNode* z) {
    while (z->parent->color == RED) {
        if (z->parent == z->parent->parent->left) {
            IntervalTNode* y = z->parent->parent->right;
            if (y->color == RED) {
                // 情形1
                z->parent->color = BLACK;
                y->color = BLACK;
                z->parent->parent->color = RED;
                z = z->parent->parent;
            }
            else {
                if (z == z->parent->right) {
                    // 情形2
                    z = z->parent;
                    Left_Rotate(T, z);
                }
                // 情形3
                z->parent->color = BLACK;
                z->parent->parent->color = RED;
                Right_Rotate(T, z->parent->parent);
            }
        }
        else { // 对称情况
            IntervalTNode* y = z->parent->parent->left;
            if (y->color == RED) {
                z->parent->color = BLACK;
                y->color = BLACK;
                z->parent->parent->color = RED;
                z = z->parent->parent;
            }
            else {
                if (z == z->parent->left) {
                    z = z->parent;
                    Right_Rotate(T, z);
                }
                z->parent->color = BLACK;
                z->parent->parent->color = RED;
                Left_Rotate(T, z->parent->parent);
            }
        }
    }
    T->root->color = BLACK;
}

// 插入一个新的区间到区间树中
void IntervalT_Insert(IntervalTree* T, interval inte) {
    IntervalTNode* z = (IntervalTNode *)malloc(sizeof(IntervalTNode));
    IntervalTNode* pz = NIL;
    z->key = inte.low;
    z->inte = inte;
    z->max = inte.high;
    z->color = RED;
    z->parent = NIL;
    z->left = NIL;
    z->right = NIL;

    IntervalTNode* y = NIL;
    IntervalTNode* x = T->root;
    while (x != NIL) {
        // 沿途更新 max 值
        if (z->max > x->max)
            x->max = z->max;
        y = x;
        if (z->key < x->key)
            x = x->left;
        else
            x = x->right;
    }
    z->parent = y;
    if (y == NIL)
        T->root = z;
    else if (z->key < y->key)
        y->left = z;
    else
        y->right = z;

    IntervalT_InsertFixup(T, z);
    // mayl 尝试合并叶子节点 到 父节点，仅一次
    if(z->parent != NIL && z->left == NIL && z->right == NIL ){
	    int ret = Merge(z->parent, z);
	    if(ret){
		     IntervalT_Delete(T, z);
	    }


    }
}



// 后序遍历重新计算整棵树每个结点的 max 值（用于删除后更新）
int computeMax(IntervalTNode* node) {
    if (node == NIL) return -1;
    int leftMax = computeMax(node->left);
    int rightMax = computeMax(node->right);
    node->max = Max(node->inte.high, leftMax, rightMax);
    return node->max;
}

// 删除修正：维护红黑树性质
void IntervalT_DeleteFixup(IntervalTree* T, IntervalTNode* x) {
    IntervalTNode* w;
    while (x != T->root && x->color == BLACK) {
        if (x == x->parent->left) {
            w = x->parent->right;
            if (w->color == RED) {
                w->color = BLACK;
                x->parent->color = RED;
                Left_Rotate(T, x->parent);
                w = x->parent->right;
            }
            if (w->left->color == BLACK && w->right->color == BLACK) {
                w->color = RED;
                x = x->parent;
            }
            else {
                if (w->right->color == BLACK) {
                    w->left->color = BLACK;
                    w->color = RED;
                    Right_Rotate(T, w);
                    w = x->parent->right;
                }
                w->color = x->parent->color;
                x->parent->color = BLACK;
                w->right->color = BLACK;
                Left_Rotate(T, x->parent);
                x = T->root;
            }
        }
        else {
            w = x->parent->left;
            if (w->color == RED) {
                w->color = BLACK;
                x->parent->color = RED;
                Right_Rotate(T, x->parent);
                w = x->parent->left;
            }
            if (w->left->color == BLACK && w->right->color == BLACK) {
                w->color = RED;
                x = x->parent;
            }
            else {
                if (w->left->color == BLACK) {
                    w->right->color = BLACK;
                    w->color = RED;
                    Left_Rotate(T, w);
                    w = x->parent->left;
                }
                w->color = x->parent->color;
                x->parent->color = BLACK;
                w->left->color = BLACK;
                Right_Rotate(T, x->parent);
                x = T->root;
            }
        }
    }
    x->color = BLACK;
}

// 删除区间树中的结点 z
void IntervalT_Delete(IntervalTree* T, IntervalTNode* z) {
    IntervalTNode* y = z;
    IntervalTNode* x;
    bool yOriginalColor = y->color;

    if (z->left == NIL || z->right == NIL)
        y = z;
    else {
        y = IntervalT_Successor(z);
        yOriginalColor = y->color;
    }

    if (y->left != NIL)
        x = y->left;
    else
        x = y->right;

    x->parent = y->parent;

    if (y->parent == NIL)
        T->root = x;
    else if (y == y->parent->left)
        y->parent->left = x;
    else
        y->parent->right = x;

    if (y != z) {
        z->key = y->key;
        z->inte = y->inte;
    }

    if (yOriginalColor == BLACK)
        IntervalT_DeleteFixup(T, x);

    computeMax(T->root);
    free(y);
    //delete y;
}

void printMenu() {
    cout << "\n===== Interval Tree 操作菜单 =====" << endl;
    cout << "1. 插入区间" << endl;
    cout << "2. 搜索与区间重叠的结点（返回所有重叠区间）" << endl;
    cout << "3. 删除精确匹配区间" << endl;
    cout << "4. 打印中序遍历" << endl;
    cout << "5. 打印树结构" << endl;
    cout << "6. 退出" << endl;
    cout << "请选择操作(1-6): ";
}


extern "C" {

struct IntervalTree* get_IntervalTree_handle()
{
	IntervalTree* T =(IntervalTree*) malloc (sizeof(IntervalTree));
	//T->root =(IntervalTNode*)malloc(sizeof(IntervalTNode));
	T->root = NIL;
	T->croot = (struct IntervalTNode *)malloc(sizeof (struct IntervalTNode));

	cp_tnode(T->root, NIL);
	pthread_rwlock_init(&T->Tree_rwlock, NULL);

	struct IntervalTree* T_ret = T;
	T_ret->root = T->root;
	T_ret->croot = T->croot;
	//T_ret->croot =  (struct IntervalTNode *)(T->croot);
	//T_ret->root = (struct IntervalTNode *)malloc(sizeof (struct IntervalTNode));
	//T_ret->croot = (struct IntervalTNode *)malloc(sizeof (struct IntervalTNode));

	//cout << "get interval tree root handle " << (struct IntervalTree*) T << endl;
	//fprintf(stderr, "get interval tree %p, Root-- %p \n", T_ret, T_ret->root);
	//fprintf(stderr, "get interval tree %p, cRoot-- %p \n", T_ret, T_ret->croot);
	//cp_tnode(T);
	return T_ret;
}

void do_IntervalT_Insert(struct IntervalTree* T, struct interval inte){
	//cout << "try  insert interval  , root handle is  " << (struct IntervalTree*) T << endl;
	//fprintf(stderr, "do insert to interval tree ...... \n");
	//fprintf(stderr, "insert to tree %p , root %p , croot %p\n", T, T->root, T->croot);
	//return ;
	//pthread_rwlock_wrlock(&T->Tree_rwlock);
	 IntervalT_Insert(T, inte);
	//pthread_rwlock_unlock(&T->Tree_rwlock);
	 return ;

}

void do_lock_interval_tree( struct IntervalTree* T,int rw){

	if(rw)
		pthread_rwlock_wrlock(&T->Tree_rwlock);
	else
		pthread_rwlock_rdlock(&T->Tree_rwlock);
	return;
}

void do_unlock_interval_tree( struct IntervalTree* T){
	pthread_rwlock_unlock(&T->Tree_rwlock);
}

struct  IntervalTNode * do_search_exact_interval(struct IntervalTree* T, struct interval inte){

	//fprintf(stderr, "do_search exact in Tree. %p, croot %p \n", T, T->croot);
	IntervalTNode* node = IntervalT_SearchExact(T, inte);
	if(node == NIL)
		return NULL;
	return node;
}

int do_replace_exact_interval(struct IntervalTree* T, struct interval inte){

	IntervalTNode* node = IntervalT_SearchExact(T, inte);
	if(node->key == -1)
		return 0;
	node->inte.data_low = inte.data_low;
	node->inte.data_high = inte.data_high;
	node->inte.comp_low = inte.comp_low;
	node->inte.comp_high = inte.comp_high;
	node->inte.obj_id = inte.obj_id;
	node->inte.flag = inte.flag;

	return 1;
}


void do_IntervalT_InorderWalk(struct IntervalTree* T){

	IntervalT_InorderWalk(T->root);
}

void do_IntervalT_Delete(struct IntervalTree* T, struct IntervalTNode* node)
{ 
	IntervalT_Delete(T, node);
}

}

/*
int main() {
    IntervalTree* T = new IntervalTree();
    T->root = NIL;

    // 初始构造一组测试区间（可选）
    interval initIntervals[] = { {16,21}, {8,9}, {25,30}, {5,8}, {15,23},
                                 {17,19}, {26,26}, {0,3}, {6,10}, {19,20} };
    int n = sizeof(initIntervals) / sizeof(interval);
    for (int i = 0; i < n; i++)
        IntervalT_Insert(T, initIntervals[i]);

    int op;
    while (true) {
        printMenu();
        cin >> op;
        if (op == 1) { // 插入区间
            interval newInt;
            cout << "请输入要插入的区间 (low high): ";
            cin >> newInt.low >> newInt.high;
            IntervalT_Insert(T, newInt);
            cout << "插入成功！" << endl;
        }
        else if (op == 2) { // 搜索重叠区间（改进部分：返回所有重叠区间）
            interval sInt;
            cout << "请输入搜索区间 (low high): ";
            cin >> sInt.low >> sInt.high;
            vector<IntervalTNode*> nodes = IntervalT_SearchAll(T, sInt);
            if (nodes.empty())
                cout << "未找到与该区间重叠的结点。" << endl;
            else {
                cout << "找到与该区间重叠的结点:" << endl;
                for (auto node : nodes) {
                    cout << "[" << node->inte.low << ", " << node->inte.high << "] ";
                    cout << (node->color == RED ? "Red " : "Black ");
                    cout << "Max:" << node->max << endl;
                }
            }
        }
        else if (op == 3) { // 删除精确匹配区间
            interval dInt;
            cout << "请输入待删除的区间 (low high): ";
            cin >> dInt.low >> dInt.high;
            IntervalTNode* node = IntervalT_SearchExact(T, dInt);
            if (node == NIL)
                cout << "未找到该精确区间结点。" << endl;
            else {
                IntervalT_Delete(T, node);
                cout << "删除成功！" << endl;
            }
        }
        else if (op == 4) { // 中序遍历打印
            cout << "\n当前 Interval Tree (中序遍历):" << endl;
            IntervalT_InorderWalk(T->root);
        }
        else if (op == 5) { // 打印直观树结构
            cout << "\n当前 Interval Tree 结构:" << endl;
            printBT("", T->root, false);
        }
        else if (op == 6) { // 退出
            break;
        }
        else {
            cout << "无效的选项，请重新选择！" << endl;
        }
    }

    return 0;
}

*/
