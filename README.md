# libwlb: A Wayland back-end library

This library aims to take care of 90% of the boilerplate code required to
write a Wayland back-end as a stand-alone compositor.

## What is a Wayland back-end?

A Wayland back-end is a small, lightweight compositor that is only capable
of displaying one full-screen client at a time.  This client may be a
simple full-screen client such as a simple animation or it may be another
compositor. 

The reason why I'm calling them back-ends comes from the idea of using the
Wayland protocol itself as an input/output abstraction.  Right now, the
standard answer to "I want to write a back-end for platform Y" is to write
it as a Weston plug-in.  The problem is that the Weston back-end plug-in
architecture is unstable and is not universal across compositors.  My
vision with Wayland back-ends is to provide a standard way for compositors
and back-ends to talk.  This would allow someone to write a stand-alone RDP
or VNC server, for instance, and then use it with any any compositor that
supports running nested inside another compositor with wl_fullscreen_shell.

### Why not just develop a standard plug-in architecture?

Mozilla did that for web browser plugins.  However, plug-ins are
notoriously unstable.  In order to solve this problem, every web browser
has developed their own mechanism for running plug-ins out-of-process.  I'm
avoiding that whole mess by running out-of-process from the start.  Also,
the Wayland protocol already provides us with all the pieces we need for
this.

### What about performance?

In theory, running out-of-process is slower because you have to communicate
over sockets and context switch between processes.  However, I don't intend
this to be used in extremely high-performance situations.  In the case of
something like a VNC or RDP server, the performance loss from the context
switch pales in comparison to the cost of network communication.

## What libwlb is designed to do

This library is designed to make it easier to write these back-ends.  There
is a certain amount of work that has to be done in order to implement
surfaces commit/attach semantics and to properly handle input.  libwlb
takes care of those nitty-gritty details for you so that the user can focus
on the back-end.

## What is libwlb *not* designed to do

libwlb is *not* designed to be a full compositor abstraction.  Using libwlb
comes with a few restrictions:

 * Only one client may be connected at a time.
 * Only one surface may be visible on a given output at a time.

libwlb does not have a full scene graph so you cannot implement a full
desktop compositor with it.  On the other hand, these assumptions keep the
API simple to make it easier to use.

## Project status

The libwlb project is currently a work-in-progress.  There are a lot of
pieces still missing and I'm sure there are lots of bugs.  The provided X
backend runs but everything should be considered pre-alpha.

The primary purpose of putting it on Github is to get feedback from the
rest of the Wayland community and possibly a little help fixing those bugs.

