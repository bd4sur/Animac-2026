#ifndef __AM_LINKER_H__
#define __AM_LINKER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>


// 功能描述：对DAG进行拓扑排序。
// 参数说明：DAG[i] = {出节点索引, 入节点索引}，表示一条从出节点指向入节点的有向边。
//          edge_num 为边的数量。
// 返回值：  成功返回排序后的节点索引数组（由调用者释放），长度等于节点总数（最大索引+1）。
//          失败（如检测到环或内存分配失败）返回 (size_t *)SIZE_MAX。
size_t *am_topo_sort(size_t DAG[][2], size_t edge_num);


#ifdef __cplusplus
}
#endif

#endif
