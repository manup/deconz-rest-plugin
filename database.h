/*
 * Copyright (c) 2016-2021 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#ifndef DATABASE_H
#define DATABASE_H

class Resource;
class ResourceItem;

bool DB_StoreSubDevice(const Resource *sub);
bool DB_StoreSubDeviceItem(const Resource *sub, const ResourceItem *item);

#endif // DATABASE_H