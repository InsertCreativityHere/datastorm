// **********************************************************************
//
// Copyright (c) 2003-2017 ZeroC, Inc. All rights reserved.
//
// This copy of Ice is licensed to you under the terms described in the
// ICE_LICENSE file included in this distribution.
//
// **********************************************************************

#pragma once

module DataStorm
{

/**
 * The sample event.
 *
 * The sample event matches the operation used by the DataWriter to update
 * the data element. It also provides information on what to expect from
 * the sample. A sample with the Add or Udpate event always provide a value
 * while a sample with the Remove type doesn't.
 *
 */
enum SampleEvent
{
    /** The element has been added. */
    Add,

    /** The element has been updated. */
    Update,

    /** The element has been partially updated. */
    PartialUpdate,

    /** The element has been removed. */
    Remove,
}

/**
 * A sequence of sample type enumeration.
 */
sequence<SampleEvent> SampleEventSeq;

}
