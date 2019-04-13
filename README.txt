<Zeyu Hu, zh16; Luoqi (Rocky) Wu, lw31>
COMP 421 Project 2: The Yalnix Operating System Kernel
<March 29, 2019>

--------------------------------------------------------------------------------------
||||||||||||||||||||||||||||||| PROJECT SUMMARY ||||||||||||||||||||||||||||||||||||||
--------------------------------------------------------------------------------------
This project builds a operating system kernel for the Yalnix operating system, running 
on a simulated computer system hardware RCS421. It can carry out an array of kernel
functionalities, including memory management, kernel calls, and terminal I/O.

--------------------------------------------------------------------------------------
||||||||||||||||||||||||||||||| DESIGN DESCRIPTION |||||||||||||||||||||||||||||||||||
--------------------------------------------------------------------------------------
Select Data Structures:

1. pcb: a structure for pcbs, which has fields:
	- pid: the process's pid
	- status: status of the process: TERMINATED, RUNNING, READY, WAITING
	- SavedContext: 
	- pfn_pt0: page table 0
	- *nextProc: next process on queue, could be any queue, since a process can only be in one queue
	- time to switch for the program
	- brk_pos: the lowest location not used by the program
	- user_stack_low_vpn: the lowest address USED by the user stack
	- child: child process of the pcb
	- next_child: the next child this process points to
	- exited_childq_head: head of the exited children process
	- exited_childq_tail: tail of the exited children process
	- parent: parent process of the pcb

2. Line: a linked list of lines in a terminal
	- ReadBuf: a buffer that contains a content of the line
	- length: length of the line
	- nextLine: the next line struct (node)

3. Terminal: a structure to record information for each terminal, there are 4 in total
	- num_char: number of chars left to read in this terminal
	- readterm_lineList_head: the head of the read line linked list for this terminal
	- readterm_lineList_tail: the tail of the read line linked list for this terminal
	- term_writing: 1 means a process is writing in this terminal, 0 means no process is writing in this terminal

4. exit_child_block: a pseudo-pcb structure for child processes that have exited
	-pid: pid of the block
	-status: its run status
	-next: the next exited child block in the FIFO queue of the parent process


5. free_physical_pages: an array[number of total physical pages] that keeps track of the free/occupied status of each
physical page, an entry is 0 if page is occupied, and 1 if it is free.

--------------------------------------------------------------------------------------

Select Global Varibles:
 
1. Ready queue head and tail: ready_q_head, ready_q_tail. They are variable to help us add and remove from ready queue

2. Delay queue head and tail:  delay_q_head, delay_q_tail. They are variable to help us add and remove from delay queue

3. 4 Block queues heads and tails for read: Read_BlockQ_0_head, Read_BlockQ_0_tail, Read_BlockQ_1_head, Read_BlockQ_1_tail, Read_BlockQ_2_head, Read_BlockQ_2_tail, Read_BlockQ_3_head, Read_BlockQ_3_tail. They are variable to help us add and remove from the terminal's corresponding Read block queue

4. 4 Block queues heads and tails for write: Write_BlockQ_0_head, Write_BlockQ_0_tail, Write_BlockQ_1_head, Write_BlockQ_1_tail, Write_BlockQ_2_head, Write_BlockQ_2_tail, Write_BlockQ_3_head, Write_BlockQ_3_tail. They are variable to help us add and remove from the terminal's corresponding Write block queue

--------------------------------------------------------------------------------------

Select Helper Function:

1. make_pcb: a function to make and initialize and new pcb for a new process. Inputs are a pfn to put new pcb, and a pid of the new process. It returns a new pcb. 

2. linkedList_add: a function to add a pcb into corresponding queue. Inputs are the queue we want to add into (ready queue, delay queue, block queue, etc), and the pcb we want to add in. 

3. linkedList_remove: a function to remove a first available process on corresponding queue (ready queue, delay queue, block queue, etc). 

4. allocate_new_pfn: no input, and it returns a new free pfn.

5. deallocate_new_pfn: input is the pfn of the page that just becomes free, the function marks this page to free.

6. print_free_list: print the free page list. 

7. lineList_add: a function that adds a new line in the linked list of lines of corresponding terminal. Inputs are the terminal number, and the Line structure we want to add.

--------------------------------------------------------------------------------------
||||||||||||||||||||||||||||||||| TESTING STRATEGY |||||||||||||||||||||||||||||||||||
--------------------------------------------------------------------------------------

We tested the kernel program mainly using the provided tests in the samples folder.
During this process, we reference to both the terminal print message and the trace file 
to make sure that our code behaves correctly. We also modified the given tests to test 
more complicated behaviors. Please see below an executive summary of test results. 

Tests 			Passed?
-----------------------------
bigstack		y
blowstack		y
brktest			y
delaytest 3		y
exectest init	y
forktest0		y
forktest1       y
forktest1b		y
forktest2       y
forktest2b		y
forktest3		y
init 			y
init1			y
init2			y
init3			y
tramemory		y
ttyread1		y
forkwait0c		y
forkwait0p		y
forkwait1 		y
forkwait1b		y
forkwait1c		y
forkwait1d		y
ttywrite1		y
ttywrite2		y
ttywrite3		y
trapillegal		y
trapmath		y
shell			y

We strived to systematically walk through each of our required functions and 
find places where the logic was left untested by the tracefiles. We believe 
that so long as we can come up with ways to test those places for errors, then 
these cases should provide a sufficiently rigorous testing model for our code.

--------------------------------------------------------------------------------------
