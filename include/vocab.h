#ifndef __AM_VOCAB_H__
#define __AM_VOCAB_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "object.h"
#include "allocator.h"


// 基础数据结构 vocab：词典，即字符串集合，实现为不重复的字符串数组，保证相同的字符串在数组中至多存在一个
// 该数据结构用于编译阶段，记录variable和symbol的集合，并通过递增index为其赋予am_varid_t或am_symbol_t
typedef struct am_vocab_t {
    am_object_t base;

    size_t  capacity; // words数组的容量
    size_t  length;   // words数组实际容纳的元素数
    wchar_t *words[]; // 弹性数组
} am_vocab_t;




// 创建词典对象，其中vocab->words初始化为长度为capacity的全0数组。
am_vocab_t *am_vocab_create(am_allocator_t *alloc,size_t capacity);

// 销毁词典对象，穿透销毁vocab->words的每一项指向的字符串
int32_t am_vocab_destroy(am_allocator_t *alloc,am_vocab_t *vocab);

// 深拷贝词典对象
am_vocab_t *am_vocab_copy(am_allocator_t *alloc, am_vocab_t *vocab);

// 将对象的二进制内存布局从alloc管理的内存中倒出来，返回一个系统malloc的二进制序列，以及序列长度
// 实现说明：将words所指向的wchar_t*宽字符串依次展平拼接，各字符串之间以L'\0'为间隔符，最后一个字符串以L'\0'结束。
//          压缩对象，将capacity压缩到跟length一致，删除多余分配的空闲部分。
//          这样，最终的内存布局就是 [size_t capacity , size_t length , wchar_t , ... , L'\0' , wchar_t , ... , ... , L'\0' ]
//          将这个结构以uint8_t数组（指针）的形式返回，size参数用于传回这个结构的字节数。
uint8_t *am_vocab_dump(am_allocator_t *alloc, am_vocab_t *vocab, size_t *size);


// 检查词典中是否存在某个词，返回其在vocab->words中的index。
// 实现提示：将word与vocab->words中的词逐个进行比较，如果有相同的，返回其index；
//           如果遍历完所有words也不存在相同的，返回SIZE_MAX，表示不存在。
size_t am_vocab_find(am_allocator_t *alloc,am_vocab_t *vocab, wchar_t *word);

// 向词典中插入一个词，返回其在vocab->words中的index。
// 实现提示：首先检查word是否存在，若存在，则返回其index，并不执行插入操作；
//           若不存在，则尝试在vocab->words尾部插入word，成功则更新length字段并返回其index，失败则返回SIZE_MAX。
size_t am_vocab_insert(am_allocator_t *alloc,am_vocab_t *vocab, wchar_t *word);

// 根据index获取词
// 实现提示：直接返回vocab->words[index]
wchar_t *am_vocab_get(am_allocator_t *alloc,am_vocab_t *vocab, size_t *index);





#ifdef __cplusplus
}
#endif

#endif
