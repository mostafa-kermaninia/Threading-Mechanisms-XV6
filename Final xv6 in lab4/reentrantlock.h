// Reentrant mutual exclusion lock
struct reentrantlock {
    struct spinlock lock;    // Underlying spinlock for atomicity
    struct proc *owner;      // Current owner of the lock
    int recursion;           // Recursion depth for reentrancy
};