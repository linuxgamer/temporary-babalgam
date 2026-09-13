#pragma once
#include "Interface.h"

class INetworkStringTable
{
public:
	virtual ~INetworkStringTable() = default;
	virtual const char* GetTableName() const = 0;
	virtual int GetTableId() const = 0;
	virtual int GetNumStrings() const = 0;
	virtual int GetMaxStrings() const = 0;
	virtual int GetEntryBits() const = 0;
	virtual void SetTick(int tick) = 0;
	virtual bool ChangedSinceTick(int tick) const = 0;
	virtual int AddString(bool bIsServer, const char* value, int length = -1, const void* userdata = nullptr) = 0;
	virtual const char* GetString(int stringNumber) const = 0;
	virtual void SetStringUserData(int stringNumber, int length, const void* userdata) = 0;
	virtual const void* GetStringUserData(int stringNumber, int* length) const = 0;
	virtual int FindStringIndex(const char* string) = 0;
};

class INetworkStringTableContainer
{
public:
	virtual ~INetworkStringTableContainer() = default;
	virtual INetworkStringTable* CreateStringTable(const char* tableName, int maxentries, int userdatafixedsize = 0, int userdatanetworkbits = 0) = 0;
	virtual void RemoveAllTables() = 0;
	virtual INetworkStringTable* FindTable(const char* tableName) const = 0;
	virtual INetworkStringTable* GetTable(int stringTable) const = 0;
	virtual int GetNumTables() const = 0;
};

MAKE_INTERFACE_VERSION(INetworkStringTableContainer, NetworkStringTableClient, "engine.dll", "VEngineClientStringTable001");
