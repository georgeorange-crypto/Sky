

// 区间结构体
/*struct interval {
    int low;
    int high;
};*/


#ifndef __INTVAL_TREE_H
#define __INTVAL_TREE_H

typedef struct interval {
    long int low;
    long int high;
    long int  obj_id;
    long int  data_low;
    long int  data_high;
    long int  comp_low;
    long int  comp_high;
    int       flag; // 0: 保持原始数据 ； 1,2.... 根据算法进行数据处理

} interval;

// 区间树结点
typedef struct IntervalTNode {
    long int key;                  // 用区间的 low 值作为 key
    int color;               // 红黑树颜色
    struct IntervalTNode* parent;    // 父节点
    struct IntervalTNode* left;      // 左孩子
    struct IntervalTNode* right;     // 右孩子

    struct interval inte;            // 存储的区间信息
    long int  max;                  // 以该结点为根的子树中所有区间 high 的最大值
} IntervalTNode;

// 区间树结构体
typedef struct IntervalTree {
    struct IntervalTNode* root;
    struct IntervalTNode* croot;
    pthread_rwlock_t rw_lock;

} IntervalTree;

//extern "C" {
  void do_IntervalT_Insert( struct IntervalTree *T, struct interval sInt);
  void do_IntervalT_InorderWalk(struct IntervalTree* T);

   void do_lock_interval_tree( struct IntervalTree* T,int rw);
   void do_unlock_interval_tree( struct IntervalTree* T );
   struct intervalTNode * do_search_exact_interval(struct IntervalTree* T, struct interval inte);
   int do_replace_exact_interval(struct IntervalTree* T, struct interval inte);
   void do_IntervalT_Delete(struct IntervalTree* T, struct IntervalTNode* node);
  struct IntervalTree * get_IntervalTree_handle();

#endif   // INTVAL_TREE_H
	 //void print_all_Interval_nodes (struct IntervalTree *T);
//}
