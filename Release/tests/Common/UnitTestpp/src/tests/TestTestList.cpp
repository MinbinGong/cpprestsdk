/***
* This file is based on or incorporates material from the UnitTest++ r30 open source project.
* Microsoft is not the original author of this code but has modified it and is licensing the code under 
* the MIT License. Microsoft reserves all other rights not expressly granted under the MIT License, 
* whether by implication, estoppel or otherwise. 
*
* UnitTest++ r30 
*
* Copyright (c) 2006 Noel Llopis and Charles Nicholson
* Portions Copyright (c) Microsoft Corporation
*
* All Rights Reserved.
*
* MIT License
*
* Permission is hereby granted, free of charge, to any person obtaining a copy of this software 
* and associated documentation files (the "Software"), to deal in the Software without restriction, 
* including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, 
* and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, 
* subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in all copies or 
* substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, 
* INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE 
* AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, 
* DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, 
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
***/

#include "stdafx.h"

using namespace UnitTest;

namespace {


TEST(TestListIsEmptyByDefault)
{
    TestList list;
    CHECK(list.GetHead() == 0);
}

TEST(AddingTestSetsHeadToTest)
{
    Test test("test");
    TestList list;
    list.Add(&test);

    CHECK(list.GetHead() == &test);
    CHECK(test.m_nextTest == 0);
}

TEST(AddingSecondTestAddsItToEndOfList)
{
    Test test1("test1");
    Test test2("test2");

    TestList list;
    list.Add(&test1);
    list.Add(&test2);

    CHECK(list.GetHead() == &test1);
    CHECK(test1.m_nextTest == &test2);
    CHECK(test2.m_nextTest == 0);
}

TEST(ListAdderAddsTestToList)
{
    TestList list;

    Test test("");    
    ListAdder adder(list, &test, nullptr);

    CHECK(list.GetHead() == &test);
    CHECK(test.m_nextTest == 0);
}

}
