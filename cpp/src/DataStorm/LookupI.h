// **********************************************************************
//
// Copyright (c) 2003-2017 ZeroC, Inc. All rights reserved.
//
// This copy of Ice is licensed to you under the terms described in the
// ICE_LICENSE file included in this distribution.
//
// **********************************************************************

#pragma once

#include <DataStorm/Contract.h>

namespace DataStormInternal
{

class TopicFactoryI;
class Instance;

class TopicLookupI : public DataStormContract::TopicLookup
{
public:

    TopicLookupI(const std::shared_ptr<TopicFactoryI>&);

    virtual void announceTopicReader(std::string, std::shared_ptr<DataStormContract::NodePrx>, const Ice::Current&);
    virtual void announceTopicWriter(std::string, std::shared_ptr<DataStormContract::NodePrx>, const Ice::Current&);

private:

    std::shared_ptr<TopicFactoryI> _factory;
};

}
