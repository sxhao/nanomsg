/*
    Copyright (c) 2013 250bpm s.r.o.

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"),
    to deal in the Software without restriction, including without limitation
    the rights to use, copy, modify, merge, publish, distribute, sublicense,
    and/or sell copies of the Software, and to permit persons to whom
    the Software is furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included
    in all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
    THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
    IN THE SOFTWARE.
*/

#include "bus.h"
#include "xbus.h"

#include "../../nn.h"
#include "../../bus.h"

#include "../../utils/cont.h"
#include "../../utils/random.h"
#include "../../utils/alloc.h"
#include "../../utils/err.h"

#include <stdint.h>

struct nn_bus {
    struct nn_xbus xbus;
    uint64_t nodeid;
};

/*  Private functions. */
static void nn_bus_init (struct nn_bus *self,
    const struct nn_sockbase_vfptr *vfptr, int fd);
static void nn_bus_term (struct nn_bus *self);

/*  Implementation of nn_sockbase's virtual functions. */
static void nn_bus_destroy (struct nn_sockbase *self);
static int nn_bus_send (struct nn_sockbase *self, struct nn_msg *msg);
static int nn_bus_recv (struct nn_sockbase *self, struct nn_msg *msg);
static int nn_bus_sethdr (struct nn_msg *msg, const void *hdr, size_t hdrlen);
static int nn_bus_gethdr (struct nn_msg *msg, void *hdr, size_t *hdrlen);
static const struct nn_sockbase_vfptr nn_bus_sockbase_vfptr = {
    nn_bus_destroy,
    nn_xbus_add,
    nn_xbus_rm,
    nn_xbus_in,
    nn_xbus_out,
    nn_bus_send,
    nn_bus_recv,
    nn_xbus_setopt,
    nn_xbus_getopt,
    nn_bus_sethdr,
    nn_bus_gethdr
};

static void nn_bus_init (struct nn_bus *self,
    const struct nn_sockbase_vfptr *vfptr, int fd)
{
    nn_xbus_init (&self->xbus, vfptr, fd);

    /*  Generate 64-bit node ID. Any incoming messages with this ID will
        be filtered out. */
    nn_random_generate (&self->nodeid, sizeof (self->nodeid));
}

static void nn_bus_term (struct nn_bus *self)
{
    nn_xbus_term (&self->xbus);
}

static void nn_bus_destroy (struct nn_sockbase *self)
{
    struct nn_bus *bus;

    bus = nn_cont (self, struct nn_bus, xbus.sockbase);

    nn_bus_term (bus);
    nn_free (bus);
}

static int nn_bus_send (struct nn_sockbase *self, struct nn_msg *msg)
{
    int rc;
    struct nn_bus *bus;

    bus = nn_cont (self, struct nn_bus, xbus.sockbase);

    /*  Tag the message with node ID. */
    nn_assert (nn_chunkref_size (&msg->hdr) == 0);
    nn_chunkref_term (&msg->hdr);
    nn_chunkref_init (&msg->hdr, sizeof (uint64_t));
    nn_putll (nn_chunkref_data (&msg->hdr), bus->nodeid);

    /*  Send the message. */
    rc = nn_xbus_send (&bus->xbus.sockbase, msg);
    errnum_assert (rc == 0, -rc);

    return 0;
}

static int nn_bus_recv (struct nn_sockbase *self, struct nn_msg *msg)
{
    int rc;
    struct nn_bus *bus;
    uint64_t nodeid;

    bus = nn_cont (self, struct nn_bus, xbus.sockbase);

    while (1) {

        /*  Get next message. */
        rc = nn_xbus_recv (&bus->xbus.sockbase, msg);
        if (nn_slow (rc == -EAGAIN))
            return -EAGAIN;
        errnum_assert (rc == 0, -rc);

        /*  Get the node ID. Drop the messages sent by this node itself. */
        if (nn_slow (nn_chunkref_size (&msg->hdr) != sizeof (uint64_t))) {
            nn_msg_term (msg);
            continue;
        }
        nodeid = nn_getll (nn_chunkref_data (&msg->hdr));
        if (nn_slow (nodeid == bus->nodeid)) {
            nn_msg_term (msg);
            continue;
        }

        /*  Discard the header and return the message to the user. */
        nn_chunkref_term (&msg->hdr);
        nn_chunkref_init (&msg->hdr, 0);
        break;
    }

    return 0;

}

static int nn_bus_sethdr (struct nn_msg *msg, const void *hdr, size_t hdrlen)
{
    if (nn_slow (hdrlen != 0))
       return -EINVAL;
    return 0;
}

static int nn_bus_gethdr (struct nn_msg *msg, void *hdr, size_t *hdrlen)
{
    *hdrlen = 0;
    return 0;
}

static struct nn_sockbase *nn_bus_create (int fd)
{
    struct nn_bus *self;

    self = nn_alloc (sizeof (struct nn_bus), "socket (bus)");
    alloc_assert (self);
    nn_bus_init (self, &nn_bus_sockbase_vfptr, fd);
    return &self->xbus.sockbase;
}

static struct nn_socktype nn_bus_socktype_struct = {
    AF_SP,
    NN_BUS,
    nn_bus_create
};

struct nn_socktype *nn_bus_socktype = &nn_bus_socktype_struct;
