#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "linker.h"


// 向邻接表中追加一个邻接节点。成功返回0，失败返回-1。
static int32_t topo_sort_push_adj(size_t **adj, size_t *len, size_t *cap, size_t node) {
    if (*len >= *cap) {
        size_t new_cap = *cap ? *cap * 2 : 4;
        size_t *new_adj = (size_t *)realloc(*adj, new_cap * sizeof(size_t));
        if (!new_adj) return -1;
        *adj = new_adj;
        *cap = new_cap;
    }
    (*adj)[(*len)++] = node;
    return 0;
}


// 功能描述：对DAG进行拓扑排序。
// 参数说明：DAG[i] = {出节点索引, 入节点索引}，表示一条从出节点指向入节点的有向边。
//          edge_num 为边的数量。
// 返回值：  成功返回排序后的节点索引数组（由调用者释放），长度等于节点总数（最大索引+1）。
//          失败（如检测到环或内存分配失败）返回 (size_t *)SIZE_MAX。
// 算法：    Kahn算法（基于入度的BFS拓扑排序）。
size_t *am_topo_sort(size_t DAG[][2], size_t edge_num) {
    if (!DAG && edge_num > 0) return (size_t *)SIZE_MAX;

    // 计算节点总数：取所有边中最大索引 + 1
    size_t node_count = 0;
    for (size_t i = 0; i < edge_num; i++) {
        if (DAG[i][0] >= node_count) node_count = DAG[i][0] + 1;
        if (DAG[i][1] >= node_count) node_count = DAG[i][1] + 1;
    }

    size_t *in_degree = (size_t *)calloc(node_count, sizeof(size_t));
    size_t *adj_len = (size_t *)calloc(node_count, sizeof(size_t));
    size_t *adj_cap = (size_t *)calloc(node_count, sizeof(size_t));
    size_t **adj = (size_t **)calloc(node_count, sizeof(size_t *));

    if ((!in_degree || !adj_len || !adj_cap || !adj) && node_count > 0) {
        free(in_degree);
        free(adj_len);
        free(adj_cap);
        free(adj);
        return (size_t *)SIZE_MAX;
    }

    // 构建邻接表和入度数组
    for (size_t i = 0; i < edge_num; i++) {
        size_t out = DAG[i][0];
        size_t in = DAG[i][1];
        in_degree[in]++;
        if (topo_sort_push_adj(&adj[out], &adj_len[out], &adj_cap[out], in) < 0) {
            for (size_t j = 0; j < node_count; j++) free(adj[j]);
            free(in_degree);
            free(adj_len);
            free(adj_cap);
            free(adj);
            return (size_t *)SIZE_MAX;
        }
    }

    size_t *result = (size_t *)malloc(node_count * sizeof(size_t));
    size_t *queue = (size_t *)malloc(node_count * sizeof(size_t));

    if ((!result || !queue) && node_count > 0) {
        free(result);
        free(queue);
        for (size_t j = 0; j < node_count; j++) free(adj[j]);
        free(in_degree);
        free(adj_len);
        free(adj_cap);
        free(adj);
        return (size_t *)SIZE_MAX;
    }

    // Kahn算法：将入度为0的节点入队
    size_t front = 0, rear = 0;
    for (size_t i = 0; i < node_count; i++) {
        if (in_degree[i] == 0) {
            queue[rear++] = i;
        }
    }

    // 依次取出节点，并将其邻接节点入度减1
    size_t result_idx = 0;
    while (front < rear) {
        size_t node = queue[front++];
        result[result_idx++] = node;

        for (size_t i = 0; i < adj_len[node]; i++) {
            size_t neighbor = adj[node][i];
            if (--in_degree[neighbor] == 0) {
                queue[rear++] = neighbor;
            }
        }
    }

    for (size_t j = 0; j < node_count; j++) free(adj[j]);
    free(in_degree);
    free(adj_len);
    free(adj_cap);
    free(adj);
    free(queue);

    // 若结果节点数不足，说明图中存在环
    if (result_idx != node_count) {
        free(result);
        return (size_t *)SIZE_MAX;
    }

    return result;
}
