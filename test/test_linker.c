#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <assert.h>

#include "linker.h"


// 计算节点总数，便于调用者知道结果数组长度
static size_t count_nodes(size_t DAG[][2], size_t edge_num) {
    size_t node_count = 0;
    for (size_t i = 0; i < edge_num; i++) {
        if (DAG[i][0] >= node_count) node_count = DAG[i][0] + 1;
        if (DAG[i][1] >= node_count) node_count = DAG[i][1] + 1;
    }
    return node_count;
}


static void print_dag(const char *name, size_t DAG[][2], size_t edge_num) {
    printf("DAG: %s\n", name);
    printf("  edges (out -> in):\n");
    for (size_t i = 0; i < edge_num; i++) {
        printf("    %zu -> %zu\n", DAG[i][0], DAG[i][1]);
    }
}


static void print_result(size_t *result, size_t node_count) {
    if (result == (size_t *)SIZE_MAX) {
        printf("  result: (size_t *)SIZE_MAX  [failure / cycle detected]\n");
        return;
    }
    printf("  topological order: [");
    for (size_t i = 0; i < node_count; i++) {
        if (i > 0) printf(", ");
        printf("%zu", result[i]);
    }
    printf("]\n");
}


// 验证排序是否合法：对每条边 out->in，out 在 result 中的位置必须早于 in
static int check_valid(size_t DAG[][2], size_t edge_num, size_t *result, size_t node_count) {
    if (result == (size_t *)SIZE_MAX) return 0;

    size_t *pos = (size_t *)malloc(node_count * sizeof(size_t));
    if (!pos) return 0;
    for (size_t i = 0; i < node_count; i++) {
        pos[result[i]] = i;
    }
    int ok = 1;
    for (size_t i = 0; i < edge_num; i++) {
        if (pos[DAG[i][0]] >= pos[DAG[i][1]]) {
            ok = 0;
            break;
        }
    }
    free(pos);
    return ok;
}


static void test_topo_sort_chain(void) {
    printf("\n[test_topo_sort_chain]\n");
    size_t DAG[][2] = {
        {0, 1},
        {1, 2},
        {2, 3},
    };
    size_t edge_num = 3;
    size_t node_count = count_nodes(DAG, edge_num);

    print_dag("chain 0->1->2->3", DAG, edge_num);
    size_t *result = am_topo_sort(DAG, edge_num);
    print_result(result, node_count);

    assert(result != (size_t *)SIZE_MAX);
    assert(check_valid(DAG, edge_num, result, node_count));
    free(result);
    printf("OK\n");
}


static void test_topo_sort_diamond(void) {
    printf("\n[test_topo_sort_diamond]\n");
    size_t DAG[][2] = {
        {0, 1},
        {0, 2},
        {1, 3},
        {2, 3},
    };
    size_t edge_num = 4;
    size_t node_count = count_nodes(DAG, edge_num);

    print_dag("diamond 0 -> {1,2} -> 3", DAG, edge_num);
    size_t *result = am_topo_sort(DAG, edge_num);
    print_result(result, node_count);

    assert(result != (size_t *)SIZE_MAX);
    assert(check_valid(DAG, edge_num, result, node_count));
    free(result);
    printf("OK\n");
}


static void test_topo_sort_cycle(void) {
    printf("\n[test_topo_sort_cycle]\n");
    size_t DAG[][2] = {
        {0, 1},
        {1, 2},
        {2, 0},
    };
    size_t edge_num = 3;
    size_t node_count = count_nodes(DAG, edge_num);

    print_dag("cycle 0->1->2->0", DAG, edge_num);
    size_t *result = am_topo_sort(DAG, edge_num);
    print_result(result, node_count);

    assert(result == (size_t *)SIZE_MAX);
    printf("OK\n");
}


static void test_topo_sort_empty(void) {
    printf("\n[test_topo_sort_empty]\n");
    size_t *result = am_topo_sort(NULL, 0);
    print_result(result, 0);

    // 空图应返回非失败指针（即使长度为0），调用者可安全释放
    assert(result != (size_t *)SIZE_MAX);
    free(result);
    printf("OK\n");
}


static void test_topo_sort_disconnected(void) {
    printf("\n[test_topo_sort_disconnected]\n");
    size_t DAG[][2] = {
        {0, 1},
        {3, 4},
    };
    size_t edge_num = 2;
    size_t node_count = count_nodes(DAG, edge_num);

    print_dag("disconnected {0->1, 3->4}", DAG, edge_num);
    size_t *result = am_topo_sort(DAG, edge_num);
    print_result(result, node_count);

    assert(result != (size_t *)SIZE_MAX);
    assert(check_valid(DAG, edge_num, result, node_count));
    free(result);
    printf("OK\n");
}


int main(void) {
    printf("=== Topological Sort Tests ===\n");

    test_topo_sort_chain();
    test_topo_sort_diamond();
    test_topo_sort_cycle();
    test_topo_sort_empty();
    test_topo_sort_disconnected();

    printf("\nAll topological sort tests passed.\n");
    return 0;
}
