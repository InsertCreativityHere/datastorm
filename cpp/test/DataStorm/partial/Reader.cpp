// **********************************************************************
//
// Copyright (c) 2018-present ZeroC, Inc. All rights reserved.
//
// **********************************************************************

#include <DataStorm/DataStorm.h>

#include <TestCommon.h>
#include <Test.h>

using namespace DataStorm;
using namespace std;
using namespace Test;

int
main(int argc, char* argv[])
{
    Node node(argc, argv);

    Topic<string, Stock> topic(node, "topic");

    ReaderConfig config;
    config.sampleCount = -1;
    config.clearHistory = ClearHistoryPolicy::Never;
    topic.setReaderDefaultConfig(config);

    topic.setUpdater<float>("price", [](Stock& stock, float price)
                            {
                                stock.price = price;
                            });

    {
        auto reader = makeSingleKeyReader(topic, "AAPL");
        auto sample = reader.getNextUnread();
        test(sample.getEvent() == SampleEvent::Add);
        test(sample.getValue().price == 12.0f);

        sample = reader.getNextUnread();
        test(sample.getEvent() == SampleEvent::PartialUpdate);
        test(sample.getUpdateTag() == "price");
        test(sample.getValue().price == 15.0f);

        sample = reader.getNextUnread();
        test(sample.getEvent() == SampleEvent::PartialUpdate);
        test(sample.getUpdateTag() == "price");
        test(sample.getValue().price == 18.0f);

        // Late joining reader should still receive update events instead of partial updates
        auto reader2 = makeSingleKeyReader(topic, "AAPL");
        sample = reader2.getNextUnread();
        test(sample.getEvent() == SampleEvent::Add);
        test(sample.getValue().price == 12.0f);

        sample = reader2.getNextUnread();
        test(sample.getEvent() == SampleEvent::PartialUpdate);
        test(sample.getValue().price == 15.0f);

        sample = reader2.getNextUnread();
        test(sample.getEvent() == SampleEvent::PartialUpdate);
        test(sample.getValue().price == 18.0f);

        // Late joining reader with limited sample count should receive one Update event and a PartialUpdate event
        auto reader3 = makeSingleKeyReader(topic, "AAPL", ReaderConfig(2));
        sample = reader3.getNextUnread();
        test(sample.getEvent() == SampleEvent::Update);
        test(sample.getValue().price == 15.0f);

        sample = reader3.getNextUnread();
        test(sample.getEvent() == SampleEvent::PartialUpdate);
        test(sample.getValue().price == 18.0f);
    }

    return 0;
}
