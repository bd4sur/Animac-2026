#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "object.h"
#include "allocator.h"
#include "wstring.h"


// 创建并初始化一个字符串对象。字符串对象是不可变的。
// 注意：am_wstring_t.content是am_value_t数组，每个元素是一个am_wchar_t。
am_wstring_t *am_wstring_create(am_allocator_t *alloc, wchar_t *str, size_t length) {
    if (!alloc || !str) return NULL;

    am_wstring_t *ws = (am_wstring_t *)am_malloc(alloc, sizeof(am_wstring_t) + length * sizeof(am_value_t));
    if (!ws) return NULL;

    ws->base.header = 0;
    ws->base.hash = 0;
    ws->base.gcmark = 0;
    ws->base.type = AM_OBJECT_TYPE_WSTRING;
    ws->length = length;
    for (size_t i = 0; i < length; i++) {
        ws->content[i] = am_make_value_of_wchar((am_wchar_t)str[i]);
    }
    return ws;
}


// 销毁对象。obj 为 NULL 时视为成功。成功返回 0，失败返回 -1。
int32_t am_wstring_destroy(am_allocator_t *alloc, am_wstring_t *obj) {
    if (!obj) return 0;
    if (!alloc) return -1;
    am_free(alloc, obj);
    return 0;
}


// 功能说明：拷贝wstring对象。成功则返回新副本对象的指针，失败则返回NULL。
am_wstring_t *am_wstring_copy(am_allocator_t *alloc, am_wstring_t *obj) {
    if (!alloc || !obj) return NULL;

    size_t total_size = sizeof(am_wstring_t) + obj->length * sizeof(am_value_t);
    am_wstring_t *copy = (am_wstring_t *)am_malloc(alloc, total_size);
    if (!copy) return NULL;

    copy->base = obj->base;
    copy->length = obj->length;
    if (obj->length > 0) {
        memcpy(copy->content, obj->content, obj->length * sizeof(am_value_t));
    }
    return copy;
}


// 转储格式：
//   [sizeof(size_t) bytes] 字符串长度（字符个数）
//   [length * sizeof(am_value_t) bytes] 字符内容（每个字符为一个am_value_t）
// 注：不额外写入对象类型标记，因为am_wstring_t.base.type已经标识了对象类型。

// 功能说明：将字符串对象序列化成二进制序列，并转储到buffer[offset]
// 实现说明：offset是写入buffer的起点offset。成功则返回向buffer新增字节数，失败则返回SIZE_MAX。
// 注意：若buffer设为NULL，或者offset设为SIZE_MAX，则仅计算转储后的二进制序列的字节数，不实际写入buffer。
size_t am_wstring_dump(am_allocator_t *alloc, am_wstring_t *obj, uint8_t *buffer, size_t offset) {
    (void)alloc;
    if (!obj) return SIZE_MAX;

    size_t content_size = obj->length * sizeof(am_value_t);
    size_t total_size = sizeof(size_t) + content_size;

    if (buffer != NULL && offset != SIZE_MAX) {
        memcpy(&buffer[offset], &obj->length, sizeof(size_t));
        if (content_size > 0) {
            memcpy(&buffer[offset + sizeof(size_t)], obj->content, content_size);
        }
    }

    return total_size;
}


// 功能说明：转储（dump）操作的逆操作。从二进制字节序列buffer[offset]开始，读取转储的字符串对象，构造字符串对象并返回其指针。
// 实现说明：offset是读取buffer的起点offset。成功则返回加载后am_wstring_t对象的指针，失败则返回NULL。
am_wstring_t *am_wstring_load(am_allocator_t *alloc, uint8_t *buffer, size_t offset) {
    if (!alloc || !buffer) return NULL;

    size_t length;
    memcpy(&length, &buffer[offset], sizeof(size_t));

    am_wstring_t *ws = (am_wstring_t *)am_malloc(alloc, sizeof(am_wstring_t) + length * sizeof(am_value_t));
    if (!ws) return NULL;

    ws->base.header = 0;
    ws->base.hash = 0;
    ws->base.gcmark = 0;
    ws->base.type = AM_OBJECT_TYPE_WSTRING;
    ws->length = length;
    if (length > 0) {
        memcpy(ws->content, &buffer[offset + sizeof(size_t)], length * sizeof(am_value_t));
    }
    return ws;
}
