/* Copyright (C) 2003-2013 Runtime Revolution Ltd.

This file is part of LiveCode.

LiveCode is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License v3 as published by the Free
Software Foundation.

LiveCode is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with LiveCode.  If not see <http://www.gnu.org/licenses/>.  */

#include <foundation.h>
#include <foundation-auto.h>
#include <foundation-stdlib.h>

#include "foundation-private.h"

////////////////////////////////////////////////////////////////////////////////

// Returns true if the list is indirect.
bool MCProperListIsIndirect(MCProperListRef self);

// Creates an indirect mutable list with contents.
static bool __MCProperListCreateIndirect(__MCProperList *contents, __MCProperList*& r_list);

// Returns true if the list is indirect.
static bool __MCProperListIsIndirect(__MCProperList *self);

// Replaces all the values in the list with immutable copies.
static bool __MCProperListMakeContentsImmutable(__MCProperList *self);

// Creates an immutable list from this one, changing 'self' to indirect.
static bool __MCProperListMakeIndirect(__MCProperList *self);

// Ensures the given mutable but indirect list is direct.
static bool __MCProperListResolveIndirect(__MCProperList *self);

static bool __MCProperListExpandAt(MCProperListRef self, uindex_t p_at, uindex_t p_count);

static bool __MCProperListShrinkAt(MCProperListRef self, uindex_t p_at, uindex_t p_count);

static void __MCProperListClampRange(MCProperListRef self, MCRange& x_range);

////////////////////////////////////////////////////////////////////////////////

bool MCProperListCreate(const MCValueRef *p_values, uindex_t p_length, MCProperListRef& r_list)
{
	bool t_success;
	t_success = true;

	MCProperListRef t_list;
	t_list = nil;
	if (t_success)
		t_success = MCProperListCreateMutable(t_list);

	if (t_success)
        t_success = MCProperListPushElements(t_list, p_values, p_length);

	if (t_success)
		return MCProperListCopyAndRelease(t_list, r_list);

	MCValueRelease(t_list);
	return false;
}

bool MCProperListCreateMutable(MCProperListRef& r_list)
{
	if (!__MCValueCreate(kMCValueTypeCodeProperList, r_list))
		return false;

	r_list -> flags |= kMCProperListFlagIsMutable;

	return true;
}	

bool MCProperListCopy(MCProperListRef self, MCProperListRef& r_new_list)
{
	// If we aren't mutable, then we can just copy directly.
	if (!MCProperListIsMutable(self))
	{
		r_new_list = MCValueRetain(self);
		return true;
	}

	// If we mutable, but still indirect just take the contents list.
	if (__MCProperListIsIndirect(self))
	{
		r_new_list = MCValueRetain(self -> contents);
		return true;
	}

	// Make the contents immutable.
	if (!__MCProperListMakeContentsImmutable(self))
		return false;

	// Make the array indirect.
	if (!__MCProperListMakeIndirect(self))
		return false;

	// Return a copy of the contents.
	r_new_list = MCValueRetain(self -> contents);
	return true;
}

bool MCProperListCopyAndRelease(MCProperListRef self, MCProperListRef& r_new_list)
{
	// If we aren't mutable, then new list is just us.
	if (!MCProperListIsMutable(self))
	{
		r_new_list = self;
		return true;
	}

	// If we are indirect, then new list is the contents, we are released.
	if (__MCProperListIsIndirect(self))
	{
		r_new_list = MCValueRetain(self -> contents);
		MCValueRelease(self);
		return true;
	}

	// We need to make an immutable copy, so first change all our contents
	// to immutable.
	if (!__MCProperListMakeContentsImmutable(self))
		return false;

	// If we have a reference count of one 'self' becomes the immutable copy.
	if (self -> references == 1)
	{
		// We are no longer immutable.
		self -> flags &= ~kMCProperListFlagIsMutable;

		// Return this as the new list.
		r_new_list = self;
		return true;
	}

	// Otherwise we must build a new indirect value.
	if (!__MCProperListMakeIndirect(self))
		return false;

	// Reduce our reference count.
	self -> references -= 1;

	// Return a copy of the contents.
	r_new_list = MCValueRetain(self -> contents);
	return true;
}

bool MCProperListMutableCopy(MCProperListRef self, MCProperListRef& r_new_list)
{
	// If the list is immutable, then the new mutable list will be indirect
	// referencing it. [ non-mutable lists cannot be indirect so self does not
	// need resolving ].
	if (!MCProperListIsMutable(self))
		return __MCProperListCreateIndirect(self, r_new_list);

	// If the list is already indirect, we just create a new reference to the
	// same contents.
	if (__MCProperListIsIndirect(self))
		return __MCProperListCreateIndirect(self -> contents, r_new_list);

	// If the list is mutable, we make it immutable and indirect and share
	// the indirect copy.
	if (!__MCProperListMakeContentsImmutable(self))
		return false;

	// Make self indirect, and fetch its contents.
	if (!__MCProperListMakeIndirect(self))
		return false;

	// Finally, create a new indirect list with contents of self.
	return __MCProperListCreateIndirect(self -> contents, r_new_list);
}

bool MCProperListMutableCopyAndRelease(MCProperListRef self, MCProperListRef& r_new_list)
{
	if (self -> references == 1)
	{
		if (!MCProperListIsMutable(self))
			self -> flags |= kMCProperListFlagIsMutable;

		r_new_list = self;
		return true;
	}

	if (!MCProperListMutableCopy(self, r_new_list))
		return false;

	self -> references -= 1;
	return true;
}

////////////////////////////////////////////////////////////////////////////////

bool MCProperListIsMutable(MCProperListRef self)
{
	return (self -> flags & kMCProperListFlagIsMutable) != 0;
}

bool MCProperListIsIndirect(MCProperListRef self)
{
	return (self -> flags & kMCProperListFlagIsIndirect) != 0;
}

uindex_t MCProperListGetLength(MCProperListRef self)
{
	if (!__MCProperListIsIndirect(self))
		return self -> length;
	return self -> contents -> length;
}

bool MCProperListIsEmpty(MCProperListRef self)
{
	if (!__MCProperListIsIndirect(self))
        return (self -> length == 0);
    
    return (self -> contents -> length == 0);
}

////////////////////////////////////////////////////////////////////////////////

bool MCProperListPushElements(MCProperListRef self, const MCValueRef *p_values, uindex_t p_length)
{
	MCAssert(MCProperListIsMutable(self));
    
    if (__MCProperListIsIndirect(self))
        if (!__MCProperListResolveIndirect(self))
            return false;
    
    uindex_t t_old_length, t_new_length;
    t_old_length = self -> length;
    t_new_length = t_old_length + p_length;
    
    if (!MCMemoryResizeArray(t_new_length, self -> list, self -> length))
        return false;
    
    for (uindex_t i = 0; i < p_length; i++)
        self -> list[i + t_old_length] = MCValueRetain(p_values[i]);
    
    return true;
}

bool MCProperListPushElement(MCProperListRef self, const MCValueRef p_value)
{
    return MCProperListPushElements(self, &p_value, 1);
}

bool MCProperListAppendList(MCProperListRef self, MCProperListRef p_value)
{
    if (MCProperListIsIndirect(p_value))
        self = p_value -> contents;
    
    return MCProperListPushElements(self, p_value -> list, p_value -> length);
}

bool MCProperListInsertElements(MCProperListRef self, const MCValueRef *p_values, uindex_t p_length, index_t p_index)
{
    MCAssert(MCProperListIsMutable(self));
    
    if (__MCProperListIsIndirect(self))
        if (!__MCProperListResolveIndirect(self))
            return false;
    
    if (!__MCProperListExpandAt(self, p_index, p_length))
        return false;
    
    for (uindex_t i = 0; i < p_length; i++)
        self -> list[i + p_index] = MCValueRetain(p_values[i]);
    
    return true;
}

bool MCProperListInsertElement(MCProperListRef self, MCValueRef p_value, index_t p_index)
{
    return MCProperListInsertElements(self, &p_value, 1, p_index);
}

bool MCProperListInsertList(MCProperListRef self, MCProperListRef p_value, index_t p_index)
{
    if (MCProperListIsIndirect(p_value))
        self = p_value -> contents;
    
    return MCProperListInsertElements(self, p_value -> list, p_value -> length, p_index);
}

bool MCProperListRemoveElements(MCProperListRef self, index_t p_start, index_t p_finish)
{
    MCAssert(MCProperListIsMutable(self));
    
    if (__MCProperListIsIndirect(self))
        if (!__MCProperListResolveIndirect(self))
            return false;
    
    MCAutoArray<MCValueRef> t_values;
    for (uindex_t i = p_start; i <= p_finish; i++)
        t_values . Push(self -> list[i]);
    
    if (__MCProperListShrinkAt(self, p_start, p_start - p_finish + 1))
        return false;
    
    for (uindex_t i = 0; i <= t_values . Size(); i++)
        MCValueRelease(t_values[i]);
    
    return true;
}

bool MCProperListRemoveElement(MCProperListRef self, index_t p_index)
{
    MCAssert(MCProperListIsMutable(self));
    
    if (__MCProperListIsIndirect(self))
        if (!__MCProperListResolveIndirect(self))
            return false;
    
    MCValueRef t_value;
    t_value = self -> list[p_index];
    
    if (__MCProperListShrinkAt(self, p_index, 1))
        return false;
    
    MCValueRelease(t_value);
    return true;
}

////////////////////////////////////////////////////////////////////////////////

MCValueRef MCProperListFetchHead(MCProperListRef self)
{
    if (MCProperListIsIndirect(self))
        self = self -> contents;
    
    return self -> list[0];
}

MCValueRef MCProperListFetchTail(MCProperListRef self)
{
    if (MCProperListIsIndirect(self))
        self = self -> contents;
    
    return self -> list[self -> length - 1];
}

MCValueRef MCProperListFetchElementAtIndex(MCProperListRef self, uindex_t p_index)
{
    if (MCProperListIsIndirect(self))
        self = self -> contents;
    
    if (p_index >= self -> length)
        return kMCNull;
    
    return self -> list[p_index];
}

bool MCProperListPop(MCProperListRef self, MCValueRef& r_value)
{
    MCAssert(MCProperListIsMutable(self));
    
    if (__MCProperListIsIndirect(self))
        if (!__MCProperListResolveIndirect(self))
            return false;
    
    MCValueRef t_value;
    t_value = self -> list[self -> length - 1];
    
    if (__MCProperListShrinkAt(self, self -> length - 1, 1))
        return false;
    
    r_value = t_value;
    return true;
}

bool MCProperListCopySublist(MCProperListRef self, MCRange p_range, MCProperListRef& r_elements)
{
    if (MCProperListIsIndirect(self))
        self = self -> contents;
    
    __MCProperListClampRange(self, p_range);
    return MCProperListCreate(self -> list + p_range . offset, p_range . length, r_elements);
}

////////////////////////////////////////////////////////////////////////////////

bool MCProperListFirstIndexOfElement(MCProperListRef self, MCValueRef p_needle, uindex_t p_after, uindex_t& r_offset)
{
    if (MCProperListIsIndirect(self))
        self = self -> contents;
    
    for (uindex_t t_offset = p_after; t_offset < self -> length; t_offset++)
    {
        if (MCValueIsEqualTo(p_needle, self -> list[t_offset]))
        {
            r_offset = t_offset;
            return true;
        }
    }
    
    return false;
}

bool MCProperListFirstIndexOfList(MCProperListRef self, MCProperListRef p_needle, uindex_t p_after, uindex_t& r_offset)
{
    if (MCProperListIsIndirect(p_needle))
        p_needle = p_needle -> contents;
    
    if (p_needle -> length == 0)
        return false;
    
    if (MCProperListIsIndirect(self))
        self = self -> contents;
    
    uindex_t t_offset, t_new_offset;
    t_offset = p_after;
    
    bool t_match;
    t_match = false;
    
    while (!t_match && MCProperListFirstIndexOfElement(self, p_needle -> list[0], t_offset, t_new_offset))
    {
        t_match = true;
        for (uindex_t i = 1; i < p_needle -> length; i++)
        {
            if (!MCValueIsEqualTo(p_needle -> list[i], self -> list[t_new_offset + i]))
            {
                t_match = false;
                break;
            }
        }
        t_offset = t_new_offset + 1;
    }
  
    if (t_match)
        r_offset = t_new_offset;
    
    return t_match;
}

////////////////////////////////////////////////////////////////////////////////

bool MCProperListCombineWithDelimiter(MCProperListRef self, MCStringRef p_delimiter, MCStringRef& r_list)
{
    if (MCProperListIsIndirect(self))
        self = self -> contents;
    
    MCAutoStringRef t_list_string;
    for (uindex_t i = 0; i < self -> length; i++)
    {
        if (MCValueGetTypeCode(self -> list[i]) != kMCValueTypeCodeString)
            return false;
        
        if (i == 0)
        {
            if (!MCStringMutableCopy((MCStringRef)self -> list[i], &t_list_string))
                return false;
        }
        else
        {
            if (!MCStringAppendFormat(*t_list_string, "%@%@", p_delimiter, (MCStringRef)self -> list[i]))
                return false;
        }
    }
    
    return MCStringCopy(*t_list_string, r_list);
}

static bool MCProperListDoSort(MCValueRef *list, uindex_t p_item_count, MCValueRef *p_temp, MCProperListSortType p_type, bool p_ascending)
{
    return false;
}

bool MCProperListSort(MCProperListRef self, bool p_ascending, MCProperListSortType p_sort_type)
{
    // TODO - implement sorting.
    uindex_t t_item_count;
    t_item_count = MCProperListGetLength(self);
    
    if (t_item_count < 2)
        return true;
    
    MCAssert(MCProperListIsMutable(self));
    
    if (__MCProperListIsIndirect(self))
        if (!__MCProperListResolveIndirect(self))
            return false;
    
    MCValueRef *t_temp_array = new MCValueRef[t_item_count];
    
    bool t_success;
    t_success = MCProperListDoSort(self -> list, t_item_count, t_temp_array, p_sort_type, p_ascending);
    
    delete[] t_temp_array;
    
    return false;
}

bool MCProperListIsEqualTo(MCProperListRef self, MCProperListRef p_other)
{
    return __MCProperListIsEqualTo(self, p_other);
}

////////////////////////////////////////////////////////////////////////////////

void __MCProperListDestroy(__MCProperList *self)
{
	if (__MCProperListIsIndirect(self))
		MCValueRelease(self -> contents);
	else
	{
		for(uindex_t i = 0; i < self -> length; i++)
			MCValueRelease(self -> list[i]);

		MCMemoryDeleteArray(self -> list);
	}
}

hash_t __MCProperListHash(__MCProperList *self)
{
	return (hash_t)self -> length;
}

bool __MCProperListIsEqualTo(__MCProperList *self, __MCProperList *other_self)
{
	// If the list is indirect, get the contents.
	MCProperListRef t_contents;
	if (!__MCProperListIsIndirect(self))
		t_contents = self;
	else
		t_contents = self -> contents;

	// If the other list is indirect, get its contents.
	MCProperListRef t_other_contents;
	if (!__MCProperListIsIndirect(other_self))
		t_other_contents = other_self;
	else
		t_other_contents = other_self -> contents;

	if (t_contents -> length != t_other_contents -> length)
		return false;

	for(uindex_t i = 0; i < t_contents -> length; i++)
	{
		if (!MCValueIsEqualTo(t_contents -> list[i], t_other_contents -> list[i]))
            return false;
	}

	// If we get here it means all values in the list are the same.
	return true;
}

bool __MCProperListCopyDescription(__MCProperList *self, MCStringRef& r_string)
{
	return false;
}

bool __MCProperListImmutableCopy(__MCProperList *self, bool p_release, __MCProperList*& r_immutable_self)
{
	if (!p_release)
		return MCProperListCopy(self, r_immutable_self);

	return MCProperListCopyAndRelease(self, r_immutable_self);
}

////////////////////////////////////////////////////////////////////////////////

static void __MCProperListClampRange(MCProperListRef self, MCRange& x_range)
{
	uindex_t t_left, t_right;
	t_left = MCMin(x_range . offset, self -> length);
	t_right = MCMin(x_range . offset + MCMin(x_range . length, UINDEX_MAX - x_range . offset), self -> length);
	x_range . offset = t_left;
	x_range . length = t_right - t_left;
}

static bool __MCProperListExpandAt(MCProperListRef self, uindex_t p_at, uindex_t p_count)
{
    MCAssert(!MCProperListIsIndirect(self));
    
    if (!MCMemoryResizeArray(self -> length + p_count, self -> list, self -> length))
        return false;
    
    MCMemoryMove(self -> list + p_at + p_count, self -> list + p_at, self -> length - p_at);
    
    return true;
}

static bool __MCProperListShrinkAt(MCProperListRef self, uindex_t p_at, uindex_t p_count)
{
    MCAssert(!MCProperListIsIndirect(self));
    
    MCMemoryMove(self -> list + p_at, self -> list + p_at + p_count, self -> length - (p_at + p_count));
    
    if (!MCMemoryResizeArray(self -> length - p_count, self -> list, self -> length))
        return false;
    
    return true;
}

static bool __MCProperListCreateIndirect(__MCProperList *p_contents, __MCProperList*& r_list)
{
	MCProperListRef self;
	if (!__MCValueCreate(kMCValueTypeCodeProperList, self))
		return false;

	self -> flags |= kMCProperListFlagIsMutable | kMCProperListFlagIsIndirect;
	self -> contents = MCValueRetain(p_contents);

	r_list = self;
	return true;
};


static bool __MCProperListIsIndirect(__MCProperList *self)
{
	return (self -> flags & kMCProperListFlagIsIndirect) != 0;
}

static bool __MCProperListMakeContentsImmutable(__MCProperList *self)
{
	for(uindex_t i = 0; i < self -> length; i++)
	{
        __MCValue *t_new_value;
        if (!__MCValueImmutableCopy((__MCValue *)self -> list[i], true, t_new_value))
            return false;

        self -> list[i] = t_new_value;
	}

	return true;
}

static bool __MCProperListMakeIndirect(__MCProperList *self)
{
	// If we are already indirect, there's nothing to do.
	if (__MCProperListIsIndirect(self))
		return true;

	// Our key-values are now all immutable, so create a new immutable list
	// with them.
	MCProperListRef t_list;
	if (!__MCValueCreate(kMCValueTypeCodeProperList, t_list))
		return false;

	// Fill in our new list.
	t_list -> length = self -> length;
	t_list -> list = self -> list;

	// 'self' now becomes indirect with a reference to the new list.
	self -> flags |= kMCProperListFlagIsIndirect;
	self -> contents = t_list;
	return true;
}

static bool __MCProperListResolveIndirect(__MCProperList *self)
{
	// Make sure we are indirect.
	MCAssert(__MCProperListIsIndirect(self));

	// Fetch the contents.
	MCProperListRef t_contents;
	t_contents = self -> contents;

	// If the contents only has a single reference, then re-absorb; otherwise
	// copy.
	if (self -> contents -> references == 1)
	{
		self -> length = t_contents -> length;
		self -> list = t_contents -> list;

		t_contents -> list = nil;
		t_contents -> length = 0;
	}
	else
	{
		uindex_t t_size;
		t_size = t_contents -> length;

		if (!MCMemoryNewArray(t_size, self -> list))
			return false;

		self -> length = t_contents -> length;

		for(uindex_t i = 0; i < t_size; i++)
            self -> list[i] = MCValueRetain(t_contents -> list[i]);
	}

	// Make sure the list is no longer marked as indirect.
	self -> flags &= ~kMCProperListFlagIsIndirect;

	// Destroy the contents.
	MCValueRelease(t_contents);

	return true;
}

////////////////////////////////////////////////////////////////////////////////

MCProperListRef kMCEmptyProperList;

bool __MCProperListInitialize(void)
{
	if (!MCProperListCreate(nil, 0, kMCEmptyProperList))
		return false;

	return true;
}

void __MCProperListFinalize(void)
{
	MCValueRelease(kMCEmptyProperList);
}

////////////////////////////////////////////////////////////////////////////////
