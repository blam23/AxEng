#include <gtest/gtest.h>
#include "AxEngLib/event.h"
#include <functional>

using namespace ax;

TEST(EventHandlerTests, FireCallsSubscribedHandlers)
{
    EventHandler<int> eh;
    int sum = 0;

    eh.subscribe([&](const int& v) { sum += v; });

    eh.fire(3);
    EXPECT_EQ(sum, 3);
}

TEST(EventHandlerTests, MultipleSubscribersAllCalled)
{
    EventHandler<int> eh;
    int a = 0, b = 0;

    eh.subscribe([&](const int& v) { a += v; });
    eh.subscribe([&](const int& v) { b += v * 2; });

    eh.fire(4);
    EXPECT_EQ(a, 4);
    EXPECT_EQ(b, 8);

    eh.fire(2);
    EXPECT_EQ(a, 6);
    EXPECT_EQ(b, 12);
}

namespace
{
	struct MyEvent { int value; };

	static int freeCallsA = 0;
	static int freeCallsB = 0;

	void FreeHandlerA(const MyEvent& e) { freeCallsA += e.value; }
	void FreeHandlerB(const MyEvent& e) { freeCallsB += e.value; }
}

TEST(EventHandlerTests, Unsubscribe_RemovesOnlyMatchingLambdaType)
{
	ax::EventHandler<MyEvent> ev;
	int callA = 0;
	int callB = 0;

	auto lambdaA = [&](const MyEvent& e) { callA += e.value; };
	auto lambdaB = [&](const MyEvent& e) { callB += e.value; };

	auto ida = ev.subscribe(lambdaA);
	auto idb = ev.subscribe(lambdaB);
	EXPECT_NE(ida, idb);

	ev.fire({ 1 });
	EXPECT_EQ(callA, 1);
	EXPECT_EQ(callB, 1);

	ev.unsubscribe(ida);

	ev.fire({ 2 });
	EXPECT_EQ(callA, 1);
	EXPECT_EQ(callB, 3);
}

TEST(EventHandlerTests, Unsubscribe_WithSameStdFunctionType_DoesNotRemoveAll)
{
	ax::EventHandler<MyEvent> ev;
	int calls = 0;

	auto func = [&](const MyEvent& e) { calls += e.value; };
	
	auto id1 = ev.subscribe(func);
	auto id2 = ev.subscribe(func);
	EXPECT_NE(id1, id2);

	ev.fire({ 1 });
	EXPECT_EQ(calls, 2);

	ev.unsubscribe(id1);

	ev.fire({ 3 });
	EXPECT_EQ(calls, 5);
}

TEST(EventHandlerTests, Unsubscribe_FunctionPointers_DoesNotRemoveAllWithSameTargetType)
{
	ax::EventHandler<MyEvent> ev;
	freeCallsA = 0;
	freeCallsB = 0;

	auto ida = ev.subscribe(&FreeHandlerA);
	auto idb = ev.subscribe(&FreeHandlerB);
	EXPECT_NE(ida, idb);

	ev.fire({ 1 });

	EXPECT_EQ(freeCallsA, 1);
	EXPECT_EQ(freeCallsB, 1);

	ev.unsubscribe(idb);

	ev.fire({ 3 });
	EXPECT_EQ(freeCallsA, 4);
	EXPECT_EQ(freeCallsB, 1);
}

TEST(EventHandlerTests, Resubscribe_AfterUnsubscribe_ReaddedAndCalled)
{
	ax::EventHandler<MyEvent> ev;
	int calls = 0;

	auto handler = [&](const MyEvent& e) { calls += e.value; };

	auto id1 = ev.subscribe(handler);
	ev.fire({ 1 });
	EXPECT_EQ(calls, 1);

	ev.unsubscribe(id1);
	ev.fire({ 1 });
	EXPECT_EQ(calls, 1);

	auto id2 = ev.subscribe(handler);
	ev.fire({ 2 });
	EXPECT_EQ(calls, 3);

	EXPECT_NE(id1, id2);
}

TEST(EventHandlerTests, InterleavedSubscribesAndUnsubscribes_IDsRemainValid)
{
	ax::EventHandler<MyEvent> ev;
	int a = 0, b = 0, c = 0, d = 0;

	auto h1 = [&](const MyEvent& e) { a += e.value; };
	auto h2 = [&](const MyEvent& e) { b += e.value; };
	auto h3 = [&](const MyEvent& e) { c += e.value; };
	auto h4 = [&](const MyEvent& e) { d += e.value; };

	auto id1 = ev.subscribe(h1);
	auto id2 = ev.subscribe(h2);
	auto id3 = ev.subscribe(h3);

	ev.fire({ 1 });
	EXPECT_EQ(a, 1);
	EXPECT_EQ(b, 1);
	EXPECT_EQ(c, 1);
	EXPECT_EQ(d, 0);

	ev.unsubscribe(id2);

	ev.fire({ 2 });
	EXPECT_EQ(a, 3); 
	EXPECT_EQ(b, 1); // unchanged as h2 unsubbed
	EXPECT_EQ(c, 3); 
	EXPECT_EQ(d, 0); // unchanged as h4 not subbed

	auto id4 = ev.subscribe(h4);
	EXPECT_NE(id4, id1);
	EXPECT_NE(id4, id3);

	ev.fire({ 1 });
	EXPECT_EQ(a, 4);
	EXPECT_EQ(b, 1);
	EXPECT_EQ(c, 4);
	EXPECT_EQ(d, 1); // changed as h4 now subbed

	ev.unsubscribe(id1);
	ev.fire({ 1 });
	EXPECT_EQ(a, 4); // no change as h1 unsubbed
	EXPECT_EQ(b, 1);
	EXPECT_EQ(c, 5);
	EXPECT_EQ(d, 2);

	auto id5 = ev.subscribe(h2);
	ev.fire({ 2 });
	EXPECT_EQ(a, 4);
	EXPECT_EQ(b, 3); // changed now h2 is resubbed
	EXPECT_EQ(c, 7);
	EXPECT_EQ(d, 4);

	// unsubscribe rest in random order
	ev.unsubscribe(id3);
	ev.unsubscribe(id4);
	ev.unsubscribe(id5);

	ev.fire({ 1 });
	EXPECT_EQ(a, 4); // all unchanged as all unsubbed
	EXPECT_EQ(b, 3);
	EXPECT_EQ(c, 7);
	EXPECT_EQ(d, 4);
}