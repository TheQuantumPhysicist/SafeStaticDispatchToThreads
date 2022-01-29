# SafeStaticDispatchToThreads
A minimal example of statically dispatching calls to different subsystems running in individual threads

In this example, we have two subsystems, A and B, each running in a thread. The Wrapper launches the threads and is supposed to be the controller of each subsystem, so we launch both subsystems in two wrappers. Then, statically and safely, we can dispatch calls to any subsystem and get the result in a future. All done in generic templates with zero specializations.

This is an example to be implemented later in Rust. But can Rust do this without SFINAE? We'll have to figure out.
