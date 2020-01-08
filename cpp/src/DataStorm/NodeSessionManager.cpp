// **********************************************************************
//
// Copyright (c) 2018-present ZeroC, Inc. All rights reserved.
//
// **********************************************************************

#include <DataStorm/NodeSessionManager.h>
#include <DataStorm/ConnectionManager.h>
#include <DataStorm/Instance.h>
#include <DataStorm/ForwarderManager.h>
#include <DataStorm/NodeSessionI.h>
#include <DataStorm/TopicFactoryI.h>
#include <DataStorm/NodeI.h>
#include <DataStorm/Timer.h>
#include <DataStorm/TraceUtil.h>

using namespace std;
using namespace DataStormI;
using namespace DataStormContract;

namespace
{

class SessionForwarderI : public Ice::Blobject
{
public:

    SessionForwarderI(shared_ptr<NodeSessionManager> nodeSessionManager) :
        _nodeSessionManager(move(nodeSessionManager))
    {
    }

    virtual bool
    ice_invoke(Ice::ByteSeq inEncaps, Ice::ByteSeq&, const Ice::Current& curr)
    {
        auto pos = curr.id.name.find('-');
        if(pos != string::npos && pos < curr.id.name.length())
        {
            auto s = _nodeSessionManager->getSession(curr.id.name.substr(pos + 1));
            if(s)
            {
                auto id = Ice::Identity { curr.id.name.substr(0, pos), curr.id.category.substr(0, 1) };
                s->getConnection()->createProxy(id)->ice_invokeAsync(curr.operation, curr.mode, inEncaps, curr.ctx);
                return true;
            }
        }
        throw Ice::ObjectNotExistException(__FILE__, __LINE__, curr.id, curr.facet, curr.operation);
    }

private:

    const shared_ptr<NodeSessionManager> _nodeSessionManager;
};

}

NodeSessionManager::NodeSessionManager(const shared_ptr<Instance>& instance) :
    _instance(instance),
    _traceLevels(instance->getTraceLevels()),
    _retryCount(0)
{
}

void
NodeSessionManager::init()
{
    auto instance = getInstance();

    auto forwarder = [self=shared_from_this()](Ice::ByteSeq e, const Ice::Current& c) { self->forward(e, c); };
    _forwarder = Ice::uncheckedCast<LookupPrx>(instance->getCollocatedForwarder()->add(move(forwarder)));

    try
    {
        auto sessionForwader = make_shared<SessionForwarderI>(shared_from_this());
        instance->getObjectAdapter()->addDefaultServant(sessionForwader, "sf");
        instance->getObjectAdapter()->addDefaultServant(sessionForwader, "pf");
    }
    catch(const Ice::ObjectAdapterDeactivatedException&)
    {
    }

    auto communicator = instance->getCommunicator();
    auto connectTo = communicator->getProperties()->getProperty("DataStorm.Node.ConnectTo");
    if(!connectTo.empty())
    {
        connect(Ice::uncheckedCast<LookupPrx>(communicator->stringToProxy("DataStorm/Lookup:" + connectTo)),
                instance->getNode()->getProxy());
    }
}

shared_ptr<NodeSessionI>
NodeSessionManager::createOrGet(const shared_ptr<NodePrx>& node,
                                const shared_ptr<Ice::Connection>& connection,
                                bool forwardAnnouncements)
{
    unique_lock<mutex> lock(_mutex);

    auto p = _sessions.find(node->ice_getIdentity());
    if(p != _sessions.end())
    {
        if(p->second->getConnection() != connection)
        {
            p->second->destroy();
            _sessions.erase(p);
        }
        else
        {
            return p->second;
        }
    }

    auto instance = getInstance();

    if(!connection->getAdapter())
    {
        connection->setAdapter(instance->getObjectAdapter());
    }

    auto session = make_shared<NodeSessionI>(instance, node, connection, forwardAnnouncements);
    session->init();
    _sessions.emplace(node->ice_getIdentity(), session);

    instance->getConnectionManager()->add(node, connection, [=, self=shared_from_this()](auto connection, auto ex)
    {
        self->destroySession(node);
    });

    return session;
}

void
NodeSessionManager::announceTopicReader(const string& topic,
                                        const shared_ptr<NodePrx>& node,
                                        const shared_ptr<Ice::Connection>& connection) const
{
    unique_lock<mutex> lock(_mutex);
    if(_traceLevels->session > 1)
    {
        Trace out(_traceLevels, _traceLevels->sessionCat);
        if(connection)
        {
            out << "topic reader `" << topic << "' announced (peer = `" << node << "')";
        }
        else
        {
            out << "announcing topic reader `" << topic << "' (peer = `" << node << "')";
        }
    }
    _exclude = connection;
    auto p = _sessions.find(node->ice_getIdentity());
    if(p != _sessions.end())
    {
        _forwarder->announceTopicReader(topic, p->second->getPublicNode());
    }
    else
    {
        _forwarder->announceTopicReader(topic, node);
    }
}

void
NodeSessionManager::announceTopicWriter(const string& topic,
                                        const shared_ptr<NodePrx>& node,
                                        const shared_ptr<Ice::Connection>& connection) const
{
    unique_lock<mutex> lock(_mutex);
    if(_traceLevels->session > 1)
    {
        Trace out(_traceLevels, _traceLevels->sessionCat);
        if(connection)
        {
            out << "topic writer `" << topic << "' announced (peer = `" << node << "')";
        }
        else
        {
            out << "announcing topic writer `" << topic << "' (peer = `" << node << "')";
        }
    }
    _exclude = connection;
    auto p = _sessions.find(node->ice_getIdentity());
    if(p != _sessions.end())
    {
        _forwarder->announceTopicWriter(topic, p->second->getPublicNode());
    }
    else
    {
        _forwarder->announceTopicWriter(topic, node);
    }
}

void
NodeSessionManager::announceTopics(const StringSeq& readers,
                                   const StringSeq& writers,
                                   const shared_ptr<NodePrx>& node,
                                   const shared_ptr<Ice::Connection>& connection) const
{
    unique_lock<mutex> lock(_mutex);
    if(_traceLevels->session > 1)
    {
        Trace out(_traceLevels, _traceLevels->sessionCat);
        if(connection)
        {
            if(!readers.empty())
            {
                out << "topic reader(s) `" << readers << "' announced (peer = `" << node << "')";
            }
            if(!writers.empty())
            {
                out << "topic writer(s) `" << writers << "' announced (peer = `" << node << "')";
            }
        }
        else
        {
            if(!readers.empty())
            {
                out << "announcing topic reader(s) `" << readers << "' (peer = `" << node << "')";
            }
            if(!writers.empty())
            {
                out << "announcing topic writer(s) `" << writers << "' (peer = `" << node << "')";
            }
        }
    }
    _exclude = connection;
    auto p = _sessions.find(node->ice_getIdentity());
    if(p != _sessions.end())
    {
        _forwarder->announceTopics(readers, writers, p->second->getPublicNode());
    }
    else
    {
        _forwarder->announceTopics(readers, writers, node);
    }
}

shared_ptr<NodeSessionI>
NodeSessionManager::getSession(const Ice::Identity& node) const
{
    unique_lock<mutex> lock(_mutex);
    auto p = _sessions.find(node);
    if(p != _sessions.end())
    {
        return p->second;
    }
    return nullptr;
}

void
NodeSessionManager::forward(const Ice::ByteSeq& inEncaps, const Ice::Current& current) const
{
    for(const auto& session : _sessions)
    {
        if(session.second->getConnection() != _exclude)
        {
            auto l = session.second->getLookup();
            if(l)
            {
                l->ice_invokeAsync(current.operation, current.mode, inEncaps, current.ctx);
            }
        }
    }
    for(const auto& lookup : _connectedTo)
    {
        if(lookup.second.second->ice_getCachedConnection() != _exclude)
        {
            lookup.second.second->ice_invokeAsync(current.operation, current.mode, inEncaps, current.ctx);
        }
    }
}

void
NodeSessionManager::connect(const shared_ptr<LookupPrx>& lookup, const shared_ptr<NodePrx>& proxy)
{
    try
    {
        lookup->createSessionAsync(proxy,
                                   [=, self=shared_from_this()](auto node)
                                   {
                                       connected(node, lookup);
                                   },
                                   [=, self=shared_from_this()](auto ex)
                                   {
                                       disconnected(nullptr, lookup);
                                   });
    }
    catch(const Ice::ObjectAdapterDeactivatedException&)
    {
        disconnected(nullptr, lookup);
    }
    catch(const Ice::CommunicatorDestroyedException&)
    {
        disconnected(nullptr, lookup);
    }
}

void
NodeSessionManager::connected(const shared_ptr<NodePrx>& node, const shared_ptr<LookupPrx>& lookup)
{
    unique_lock<mutex> lock(_mutex);
    auto instance = _instance.lock();
    if(!instance)
    {
        return;
    }

    auto p = _sessions.find(node->ice_getIdentity());
    auto connection = p != _sessions.end() ? p->second->getConnection() : lookup->ice_getCachedConnection();
    if(!connection->getAdapter())
    {
        connection->setAdapter(instance->getObjectAdapter());
    }

    if(_traceLevels->session > 0)
    {
        Trace out(_traceLevels, _traceLevels->sessionCat);
        out << "established node session (peer = `" << node << "'):\n" << connection->toString();
    }

    instance->getConnectionManager()->add(lookup, connection, [=, self=shared_from_this()](auto connection, auto ex)
    {
        disconnected(node, lookup);
    });
    _connectedTo.emplace(node->ice_getIdentity(),
                         make_pair(node, p != _sessions.end() ? lookup->ice_fixed(connection) : lookup));

    try
    {
        lookup->announceTopicsAsync(instance->getTopicFactory()->getTopicReaderNames(),
                                    instance->getTopicFactory()->getTopicWriterNames(),
                                    instance->getNode()->getProxy());
    }
    catch(const Ice::ObjectAdapterDeactivatedException&)
    {
    }
    catch(const Ice::CommunicatorDestroyedException&)
    {
    }
}

void
NodeSessionManager::disconnected(const shared_ptr<NodePrx>& node, const shared_ptr<LookupPrx>& lookup)
{
    unique_lock<mutex> lock(_mutex);
    auto instance = _instance.lock();
    if(!instance)
    {
        return;
    }

    if(node != nullptr)
    {
        _retryCount = 0;
        if(_traceLevels->session > 0)
        {
            Trace out(_traceLevels, _traceLevels->sessionCat);
            out << "disconnected node session (peer = `" << node << "')";
        }
        _connectedTo.erase(node->ice_getIdentity());
        connect(lookup, instance->getNode()->getProxy());
    }
    else
    {
        instance->getTimer()->schedule(instance->getRetryDelay(_retryCount++),
                                       [=, self=shared_from_this()]
                                       {
                                           auto instance = _instance.lock();
                                           if(instance)
                                           {
                                               connect(lookup, instance->getNode()->getProxy());
                                           }
                                       });
    }
}

void
NodeSessionManager::destroySession(const shared_ptr<NodePrx>& node)
{
    unique_lock<mutex> lock(_mutex);
    auto p = _sessions.find(node->ice_getIdentity());
    if(p == _sessions.end())
    {
        return;
    }

    p->second->destroy();
    _sessions.erase(p);
}
