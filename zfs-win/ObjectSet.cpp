/* 
 *	Copyright (C) 2010 Gabest
 *	http://code.google.com/p/zfs-win/
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *   
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *   
 *  You should have received a copy of the GNU General Public License
 *  along with GNU Make; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA. 
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */

#include "stdafx.h"
#include "ObjectSet.h"

namespace ZFS
{
	ObjectSet::ObjectSet(Pool* pool)
		: m_pool(pool)
		, m_reader(NULL)
		, m_count(0)
	{
	}

	ObjectSet::~ObjectSet()
	{
		RemoveAll();
	}

	void ObjectSet::RemoveAll()
	{
		for(auto i = m_objdir.begin(); i != m_objdir.end(); i++)
		{
			delete i->second;
		}

		m_objdir.clear();

		m_cache.clear();

		delete m_reader;

		m_reader = NULL;

		m_count = 0;
	}

	bool ObjectSet::Init(blkptr_t* bp)
	{
		ASSERT(bp->type == DMU_OT_OBJSET);
		
		RemoveAll();

		ASSERT(bp->lvl == 0); // must not be indirect

		uint8_t* buff = (uint8_t*)_aligned_malloc(sizeof(m_objset), 16);

		if(m_pool->Read(buff, sizeof(m_objset), bp))
		{
			memcpy(&m_objset, buff, sizeof(m_objset));
		}
		else
		{
			memset(&m_objset, 0, sizeof(m_objset));
		}

		_aligned_free(buff);

		if(m_objset.meta_dnode.type != DMU_OT_DNODE)
		{
			return false;
		}

		m_reader = new BlockReader(m_pool, &m_objset.meta_dnode);

		m_count = m_reader->GetDataSize() / sizeof(dnode_phys_t);

		return true;
	}

	uint64_t ObjectSet::GetIndex(const char* name, uint64_t parent_index)
	{
		ZapObject* zap = NULL;

		if(!Read(parent_index, &zap))
		{
			return -1;
		}

		uint64_t index;

		if(!zap->Lookup(name, index))
		{
			return -1;
		}

		return index;
	}

	bool ObjectSet::Read(uint64_t index, dnode_phys_t* dn, dmu_object_type type)
	{
		ASSERT(index == -1 || index < UINT_MAX);

		if(index >= m_count || dn == NULL) 
		{	
			return false;
		}

		auto i = m_cache.find(index);

		if(i == m_cache.end())
		{
			size_t size = sizeof(dnode_phys_t);

			if(m_reader->Read(dn, size, index * size) != size)
			{
				return false;
			}

			dn->pad3[0] = index;
		
			if(dn->type != DMU_OT_PLAIN_FILE_CONTENTS)
			{
				m_cache[index] = *dn;
			}
		}
		else
		{
			*dn = i->second;
		}

		return type == DMU_OT_NONE || dn->type == type;
	}

	bool ObjectSet::Read(uint64_t index, ZapObject** zap, dmu_object_type type)
	{
		auto i = m_objdir.find(index);

		if(i == m_objdir.end())
		{
			dnode_phys_t dn;

			if(!Read(index, &dn, type))
			{
				return false;
			}

			switch(dn.type)
			{
			case DMU_OT_OBJECT_DIRECTORY:
			case DMU_OT_DSL_DIR_CHILD_MAP:
			case DMU_OT_DSL_DS_SNAP_MAP:
			case DMU_OT_DSL_PROPS:
			case DMU_OT_DIRECTORY_CONTENTS:
			case DMU_OT_MASTER_NODE:
			case DMU_OT_UNLINKED_SET:
			case DMU_OT_ZVOL_PROP:
			case DMU_OT_ZAP_OTHER:
			case DMU_OT_ERROR_LOG:
			case DMU_OT_POOL_PROPS:
			case DMU_OT_DSL_PERMS:
			case DMU_OT_NEXT_CLONES:
			case DMU_OT_SCRUB_QUEUE:
			case DMU_OT_USERGROUP_USED:
			case DMU_OT_USERGROUP_QUOTA:
			case DMU_OT_USERREFS:
			case DMU_OT_DDT_ZAP:
			case DMU_OT_DDT_STATS:
				break;
			default:
				printf("Not a known ZAP object (dn.type = %d)\n", dn.type);
				return false;
			}

			*zap = new ZapObject(m_pool);

			if(!(*zap)->Init(&dn))
			{
				delete *zap;

				return false;
			}

			m_objdir[index] = *zap;
		}
		else
		{
			*zap = i->second;
		}

		return true;
	}

	bool ObjectSet::Read(uint64_t index, NameValueList& nvl)
	{
		dnode_phys_t dn;

		if(!Read(index, &dn, DMU_OT_PACKED_NVLIST))
		{
			return false;
		}

		BlockReader r(m_pool, &dn);

		size_t size = (size_t)r.GetDataSize();

		std::vector<uint8_t> buff(size);

		if(r.Read(buff.data(), size, 0) != size)
		{
			return false;
		}

		if(!nvl.Init(buff.data(), buff.size()))
		{
			return false;
		}

		return true;
	}
}