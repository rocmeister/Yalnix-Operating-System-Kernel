#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#include <comp421/loadinfo.h>
#include <comp421/yalnix.h>
#include <comp421/hardware.h>


/* MACRO Definitions */
//#define PFN2ADDR(p)  p * PAGESIZE;
#define TERMINATED -1
#define RUNNING 0
#define READY 1
#define WAITING 2

/* Data Structure Definitions */
// define a trap function pointer type
typedef int (*trap_funcptr)(ExceptionInfo *);
// pcb struct, data members to be filled
// typedef struct pcb {
// 	int pid;
// 	SavedContext ctx;
// 	int pfn_pt0; // page table 0
// } pcb;
typedef struct exit_child_block {
    int pid;
    int status;
    struct exit_child_block *next;
} ecb;

// pcb struct, data members to be filled
// pcb struct, data members to be filled
typedef struct pcb {
	int pid;
	int status;
	SavedContext ctx;
	int pfn_pt0; // page table 0
	struct pcb *nextProc;
	long switchTime;
	int brk_pos;
	void *user_stack_low_vpn;
	struct pcb *child;
	struct pcb *next_child; // NEW
	struct pcb *sibling_q_head;
	struct pcb *sibling_q_tail;
	ecb *exited_childq_head; // NEW
	ecb *exited_childq_tail; // NEW
	struct pcb *parent;

	/* TTY stuff */
	struct pcb *Read_BlockQ_head;

} pcb;

/* KernelStart Routine */
// Section 3.3, 3.6
extern void KernelStart(ExceptionInfo *, unsigned int, void *, char **);
int LoadProgram(char *name, char **args, ExceptionInfo *info);

/* SetKernelBrk Routine */
// Section 3.4.2
extern int SetKernelBrk(void *);

/* Trap Handlers */
// Section 3.2
void trap_kernel(ExceptionInfo *info);
void trap_clock(ExceptionInfo *info);
void trap_illegal(ExceptionInfo *info);
void trap_memory(ExceptionInfo *info);
void trap_math(ExceptionInfo *info);
void trap_tty_receive(ExceptionInfo *info);
void trap_tty_transmit(ExceptionInfo *info);

/* Kernel Call Methods */
// Section 3.1
int Fork(void);
int ExecFunc(char *filename, char **argvec, ExceptionInfo *info);
void Exit(int) __attribute__ ((noreturn));
int Wait(int *);
int GetPid(void);
int Brk(void *);
int Delay(int);
int TtyRead(int, void *, int);
int TtyWrite(int tty_id, void *, int);

/* Context Switch */
SavedContext *MySwitchFunc(SavedContext *, void *, void *);

/* Helper Routines */
pcb *make_pcb(int pfn, int pid);

/* Global Variables */
//static int number_of_physical_pages;
//A list to keep track of all free physical pages' indices
int *free_physical_pages; // 1 if free, 0 if not free
struct pte *page_table_region_0; // temporary region 0 pt for the first process
struct pte *page_table_region_1; // region 1 pt, shared by all processes
void *end_of_kernel_heap;
//int region0_brk_pn; // the page number region brk is in 
int region0_brk = MEM_INVALID_PAGES;
int free_pages_counter; // number of free physical pages
int free_pages_pointer; // pointer to the physical pages index that was used last time
ExceptionInfo *exception_info;
int process_id = 2;
int vm_enabled;
int total_pages;
int loaded = 0;
int idle_loaded = 0;

// kernel call necessary global variables
pcb *current_pcb;
pcb *idle_pcb;
struct pte *pt0 = (struct pte*) ((VMEM_1_LIMIT) - (2 * (PAGESIZE)));
unsigned long total_time = 0;
pcb *ready_q_head = NULL, *ready_q_tail = NULL;
pcb *delay_q_head = NULL, *delay_q_tail = NULL;



// /* TTY's */
// typedef struct term
// {
//     char *BufRead[256];
//     unsigned long num_char;
//     char *BufWrite;
//     struct Read_BlockQ *read_queue;
//     struct Write_BlockQ *write_queue;
//     int writing;
// } Terminal;

// struct term terminal_0;
// struct term terminal_1;
// struct term terminal_2;
// struct term terminal_3;

/* TTY stuff */
// --------------------------------------------------------------------
#define DelayQueue 0
#define ReadyQueue 1

#define BlockQ_0 3
#define BlockQ_1 4
#define BlockQ_2 5
#define BlockQ_3 6

pcb *Read_BlockQ_0_head = NULL;
pcb *Read_BlockQ_0_tail = NULL;
pcb *Read_BlockQ_1_head = NULL;
pcb *Read_BlockQ_1_tail = NULL;
pcb *Read_BlockQ_2_head = NULL;
pcb *Read_BlockQ_2_tail = NULL;
pcb *Read_BlockQ_3_head = NULL;
pcb *Read_BlockQ_3_tail = NULL;

// pcb *Write_BlockQ_0_head = NULL;
// pcb *Write_BlockQ_0_tail = NULL;

// typedef struct Line
// {
//     char *ReadBuf;
//     int length;
//     struct Line *nextLine;   
// } Line;

// typedef struct Terminal
// {
//     //char *BufRead[256];
//     int num_char;
//     //char *BufWrite;
//     Line *readterm_lineList_head;
//     Line *readterm_lineList_tail;
//     //int writing;
// } Terminal;

// Terminal *terminal[4];

typedef struct Line
{
    char *ReadBuf;
    int length;
    struct Line *nextLine;   
} Line;

typedef struct Terminal
{
    //char *BufRead[256];
    int num_char;
    //char *BufWrite;
    Line *readterm_lineList_head;
    Line *readterm_lineList_tail;
    int term_writing; // 1 means a process is writing, 0 means no process is writing.
    Line *writeterm_lineList_head;
    Line *writeterm_lineList_tail;

} Terminal;

Terminal *terminal[4];

/* TTY Write Stuff */
// --------------------------------------------------------------------

pcb *Write_BlockQ_0_head = NULL;
pcb *Write_BlockQ_0_tail = NULL;
pcb *Write_BlockQ_1_head = NULL;
pcb *Write_BlockQ_1_tail = NULL;
pcb *Write_BlockQ_2_head = NULL;
pcb *Write_BlockQ_2_tail = NULL;
pcb *Write_BlockQ_3_head = NULL;
pcb *Write_BlockQ_3_tail = NULL;

#define Write_BlockQ_0 8
#define Write_BlockQ_1 9
#define Write_BlockQ_2 10
#define Write_BlockQ_3 11




// -------------------------------------------------------------------------------------

/* END OF DECLARATION */


/* START OF IMPLEMENTATION */

/* KernalStart Routine 
	info - pointer to an initial ExceptionInfo structure
	pmem_size - total size of the physical memory in bytes
	orig_brk - initial value of the kernel's "break"
	cmd_args - a vector of pointers to each argument from the boot command line
*/
void KernelStart(ExceptionInfo *info, unsigned int pmem_size, 
	void *orig_brk, char **cmd_args)
{
	// some book-keeping of global variables
	end_of_kernel_heap = orig_brk;
	exception_info = info;
	TracePrintf(10, "before IVTvm is\n");

	//Initialize the interrupt vector table entries and set its address to the privileged register
	InitInterruptVectorTable();
	TracePrintf(10, "done with init IVT before free list\n");

	// make a list to keep track of free physical pages
	initFreePhysicalPages(pmem_size);

	TracePrintf(10, "done with free list before page table\n");

	terminal[0] = (Terminal *) malloc(sizeof(Terminal));
	terminal[1] = (Terminal *) malloc(sizeof(Terminal));
	terminal[2] = (Terminal *) malloc(sizeof(Terminal));
	terminal[3] = (Terminal *) malloc(sizeof(Terminal));
 	//Tty

 	// terminal[0].num_char = 0;
 	// // Line *temp;
 	// terminal[0].readterm_lineList_head = NULL;
 	// terminal[1].num_char = 0;
 	// terminal[1].readterm_lineList_head = NULL;
 	// terminal[2].num_char = 0;
 	// terminal[2].readterm_lineList_head = NULL;
 	// terminal[3].num_char = 0;
 	// terminal[3].readterm_lineList_head = NULL;



	terminal[0]->num_char = 0;
 	terminal[0]->readterm_lineList_head = NULL;
 	terminal[1]->num_char = 0;
 	terminal[1]->readterm_lineList_head = NULL;
 	terminal[2]->num_char = 0;
 	terminal[2]->readterm_lineList_head = NULL;
 	terminal[3]->num_char = 0;
 	terminal[3]->readterm_lineList_head = NULL;

	// initialize region 1 & region 0 page table, initialize REG_PTR0 and REG_PTR1
	initPageTable();

	TracePrintf(10, "done with page table before vm\n");

	// enable VM
	enable_virtual_memory();

	
	// initialize init process
 	init_process(cmd_args, exception_info);
 	TracePrintf(10, "done with init proc\n");

	// initialize idle process
 	idle_process();
 

 	if (loaded == 0) {
 		loaded = 1;
		int load_status = LoadProgram(cmd_args[0], cmd_args, exception_info);
		
		TracePrintf(9, "Status of init loaded is %d\n", load_status); 		
 	} else {
 		int idle_status = LoadProgram("idle", cmd_args, exception_info);
 		
 		TracePrintf(9, "Status of idle loaded is %d\n", idle_status); 
 	}

 	TracePrintf(9, "[KernelStart] DONE \n"); 

 	// if (idle_loaded == 0) {
 	// 	idle_loaded = 1;
 	// 	int idle_status = LoadProgram("idle", cmd_args, exception_info);
 			
 	// }
	// initialize terminals

}

int SetKernelBrk(void *addr) {
	if (vm_enabled == 0) {
		end_of_kernel_heap = addr;
		return 0;
	} else {
		TracePrintf(0, "Addr of SetKernelBrk is %p\n", addr);
		TracePrintf(0, "pid %d\n SetKernelBrk", current_pcb->pid);
		int set_brk = (UP_TO_PAGE((int)addr) >> PAGESHIFT) - PAGE_TABLE_LEN;

		if (end_of_kernel_heap > VMEM_1_LIMIT - 2) {
			TracePrintf(0, "end_of_kernel_heap > VMEM_1_LIMIT - 2");
			return ERROR;
		}

		if (end_of_kernel_heap < VMEM_1_BASE) {
			TracePrintf(0, "end_of_kernel_heap < VMEM_1_BASE");
			return ERROR;
		}

		// case1: set_brk bigger than brk_pos, move up 
		int current_pcb_vpn = (UP_TO_PAGE(end_of_kernel_heap) >> PAGESHIFT) - PAGE_TABLE_LEN;
		TracePrintf(0, "Current pcb vpn is %d\n", current_pcb_vpn);
		TracePrintf(0, "Set heap to %d\n",set_brk);
		int num_pages_up = (set_brk - current_pcb_vpn);
		int i;
		for (i = 0; i < num_pages_up; i++) {
			if(current_pcb_vpn + i > PAGE_TABLE_LEN-3) {
				TracePrintf(9, "[SetKernelBrk] Error: Not enough memory \n");
				return -1;
			}
			//enqueue a page
			// TracePrintf(0, "[SetKernelBrk] Allocating more memory for vpn %")
			page_table_region_1[current_pcb_vpn + i].pfn = allocate_new_pfn();
			page_table_region_1[current_pcb_vpn+ i].valid = 1;
			page_table_region_1[current_pcb_vpn + i].uprot = PROT_NONE;
			page_table_region_1[current_pcb_vpn + i].kprot = (PROT_READ | PROT_WRITE);			
			WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_1);

		}
		end_of_kernel_heap = addr;
	}
	TracePrintf(9, "[SetKernelBrk] DONE \n"); 
	return 0;
}

void init_process(char **cmd_args, ExceptionInfo *info) {
	// step1: create new PCB
	pcb *init_pcb = (pcb *) malloc(sizeof(pcb));
	if (init_pcb == NULL) {
		fprintf(stderr, "Faliled to malloc for init process.\n");
        exit(-1);
	}
	init_pcb -> pid = 1;
	TracePrintf(9, "init process before initializing \n");
	// pcb -> ctx = malloc(sizeof(SavedContext)); do we need this step??
	// initialize the pt0 field
	init_pcb -> status = RUNNING;
	init_pcb -> pfn_pt0 = PAGE_TABLE_LEN * 2 - 2;
	init_pcb -> nextProc = NULL;
	init_pcb -> switchTime = total_time + 2;
    init_pcb -> brk_pos = region0_brk;
    init_pcb -> user_stack_low_vpn = PAGE_TABLE_LEN - KERNEL_STACK_PAGES - 2;
    init_pcb -> child = NULL;
    init_pcb -> next_child = NULL;
 	init_pcb->sibling_q_head = NULL;
	init_pcb->sibling_q_tail = NULL;
	init_pcb -> exited_childq_head = NULL;
	init_pcb -> exited_childq_tail = NULL; 
	init_pcb->parent = NULL;   

	// step 2: set current process to init
	current_pcb = init_pcb;
	TracePrintf(9, "init process before loading \n");

	// step2: ContextSwitch, so that init_pcb has a copy of the current SavedContext
	//ContextSwitch(MySwitchFunc, &init_pcb->ctx, init_pcb, init_pcb);

	// step3: load init
	//ExceptionInfo *info = malloc(sizeof(ExceptionInfo));

	// int loaded = LoadProgram(cmd_args[0], cmd_args, info);
	// TracePrintf(9, "Status of init loaded is %d\n", loaded);
	TracePrintf(9, "[init_process] DONE \n"); 
}


void idle_process(char **cmd_args) {
	idle_pcb = make_pcb(allocate_new_pfn(), 0);
	idle_pcb -> pid = 0;
	// // step1: create new PCB
	// pcb *idle_pcb = (pcb *) malloc(sizeof(pcb));
	// idle_pcb -> pid = 0;

	// // step2: allocate new free physical page to new process's pt0
	// idle_pcb -> pfn_pt0 = allocate_new_pfn();
	// //current_pcb = idle_pcb;

	// // step3: context switch to itself idle so we have savedContext for idle
	// ContextSwitch(MySwitchFunc, &idle_pcb->ctx, idle_pcb, idle_pcb);

	// // step3: ContextSwitch, copy kernel stack - find new free pages for kernel stack
	// ContextSwitch(MySwitchFunc, &init_pcb->ctx, init_pcb, idle_pcb);
	// pt1[] after context switch to idle

	// step4: load idle
	TracePrintf(9, "[idle_process] DONE \n"); 

}

/* START of Kernel Start Sub-Routines */
void InitInterruptVectorTable() {
	int j;
	// Interrupt vector Table: An array of pointers to functions whose input is *ExceptionInfo.
	// Make it locate at the 510th (3rd to last) page of region 1 memory!
	//trap_funcptr *InterruptVectorTable = VMEM_1_LIMIT - 3 * PAGESIZE;
	trap_funcptr *InterruptVectorTable = malloc(TRAP_VECTOR_SIZE * sizeof(trap_funcptr));
	
	// Define each element in the Table.
	InterruptVectorTable[TRAP_KERNEL] = trap_kernel;
	InterruptVectorTable[TRAP_CLOCK] = trap_clock;
	InterruptVectorTable[TRAP_ILLEGAL] = trap_illegal;
	InterruptVectorTable[TRAP_MEMORY] = trap_memory;
	InterruptVectorTable[TRAP_MATH] = trap_math;
	InterruptVectorTable[TRAP_TTY_RECEIVE] = trap_tty_receive;
	InterruptVectorTable[TRAP_TTY_TRANSMIT] = trap_tty_transmit;
	// initialize unused entries to NULL
	for (j = 7; j < 16; j++) {
		InterruptVectorTable[j] = NULL;
	}

	// Initialize the REG_VECTOR_BASE privileged machine register
	WriteRegister(REG_VECTOR_BASE, (RCS421RegVal) InterruptVectorTable);
	TracePrintf(9, "[InitInterruptVectorTable] DONE \n"); 
}

// some more work needs to be done
void initFreePhysicalPages(unsigned int pmem_size) {
	total_pages = DOWN_TO_PAGE(pmem_size) >> PAGESHIFT;
	
	// TODO: move the list to the semi-top of region 1!
	free_physical_pages = malloc(total_pages * sizeof(int));

	// some stuff with invalid memory
	int page_iter;
	for (page_iter = 0; page_iter < PAGE_TABLE_LEN - KERNEL_STACK_PAGES; page_iter++) {
		free_physical_pages[page_iter] = 1;
		free_pages_counter++;
	}
	int heap_pages = UP_TO_PAGE(end_of_kernel_heap - VMEM_1_BASE) >> PAGESHIFT;
	for (page_iter = PAGE_TABLE_LEN - KERNEL_STACK_PAGES; page_iter < PAGE_TABLE_LEN - KERNEL_STACK_PAGES + heap_pages; 
		page_iter++) {
		free_physical_pages[page_iter] = 0;
	}
	for (page_iter = PAGE_TABLE_LEN - KERNEL_STACK_PAGES + heap_pages; page_iter < PAGE_TABLE_LEN * 2 - 2; page_iter++) {
		free_physical_pages[page_iter] = 1;
		free_pages_counter++;
	}
	for (page_iter = PAGE_TABLE_LEN * 2 - 2; page_iter < PAGE_TABLE_LEN * 2; page_iter++) {
		free_physical_pages[page_iter] = 0;
	}
	for (page_iter = PAGE_TABLE_LEN * 2; page_iter < total_pages; page_iter++) {
		free_physical_pages[page_iter] = 1;
		free_pages_counter++;
	}
	free_pages_pointer = 0;
	TracePrintf(9, "[initFreePhysicalPages] DONE \n"); 
}

// Section 3.4.3
void initPageTable() {
	TracePrintf(10, "start of page table\n");
	//initialize region 1 & region 0 page table
	// step1: compute number of entries in each page table
	// int num_entries_0 = DOWN_TO_PAGE(VMEM_0_SIZE) >> PAGESHIFT;
	// int num_entries_1 = DOWN_TO_PAGE(VMEM_1_SIZE) >> PAGESHIFT;
	// page_table_region_0 = malloc(num_entries_0 * sizeof(struct pte));
	// page_table_region_1 = malloc(num_entries_1 * sizeof(struct pte));
	//page_table_region_0 = malloc(PAGE_TABLE_LEN * sizeof(struct pte));
	//page_table_region_1 = malloc(PAGE_TABLE_LEN * sizeof(struct pte));

	// make the initial pt0 and pt1 live on the top of region 1
	page_table_region_0 = (struct pte*) (VMEM_1_LIMIT - PAGESIZE * 2);
	page_table_region_1 = (struct pte*) (VMEM_1_LIMIT - PAGESIZE);
	//TracePrintf(10, "done with page table region 0 address: %p\n", page_table_region_0);
	
	// step2: fill in the entries with struct pte
	// first fill in the kernel stack for user process in region 0
	int pt_iter0;
	for (pt_iter0 = 0; pt_iter0 < KERNEL_STACK_PAGES; pt_iter0++) {
		int pt_index = PAGE_TABLE_LEN - pt_iter0 - 1; 
		page_table_region_0[pt_index].pfn = pt_index;
		page_table_region_0[pt_index].uprot = PROT_NONE;
		page_table_region_0[pt_index].kprot = (PROT_READ | PROT_WRITE);
		page_table_region_0[pt_index].valid = 1;
		//TracePrintf(10, "page table region 0 : %p\n", page_table_region_0);

	}
	//TracePrintf(10, "done with page table part 1\n");

	// then fill in the kernel text, data, bss, and heap in region 1
	int pt_iter1;
	for (pt_iter1 = 0; pt_iter1 < (UP_TO_PAGE((end_of_kernel_heap - VMEM_1_BASE)) >> PAGESHIFT); pt_iter1++) {
		page_table_region_1[pt_iter1].pfn = PAGE_TABLE_LEN + pt_iter1;
		page_table_region_1[pt_iter1].uprot = PROT_NONE;
		page_table_region_1[pt_iter1].valid = 1;

		if (VMEM_1_BASE + pt_iter1 * PAGESIZE < &_etext) {
			// KERNEL text
			page_table_region_1[pt_iter1].kprot = (PROT_READ | PROT_EXEC);
		} else {
			// KERNEL data/bss/heap
			page_table_region_1[pt_iter1].kprot = (PROT_READ | PROT_WRITE);
		}
		//TracePrintf(10, "pt vpn is %d\n", pt_iter1 + 512);
	}
	TracePrintf(10, "done with page table part 2\n");

	page_table_region_1[PAGE_TABLE_LEN-1].pfn = PAGE_TABLE_LEN + PAGE_TABLE_LEN-1;
	page_table_region_1[PAGE_TABLE_LEN-1].valid = 1;
	page_table_region_1[PAGE_TABLE_LEN-1].uprot = PROT_NONE;
	page_table_region_1[PAGE_TABLE_LEN-1].kprot = PROT_READ|PROT_WRITE;
	page_table_region_1[PAGE_TABLE_LEN-2].pfn = PAGE_TABLE_LEN + PAGE_TABLE_LEN-2;
	page_table_region_1[PAGE_TABLE_LEN-2].valid = 1;
	page_table_region_1[PAGE_TABLE_LEN-2].uprot = PROT_NONE;
	page_table_region_1[PAGE_TABLE_LEN-2].kprot = PROT_READ|PROT_WRITE;

	// step3: certain entries might need to be modified, skipped for now
	// mark the top n pages of region 1 as invalid

	// step4: update the free_physical_pages list!
	// int free_iter;
	// int kernel_pages = KERNEL_STACK_PAGES + (((int) end_of_kernel_heap - VMEM_1_BASE) >> PAGESHIFT);
	// for (free_iter = PAGE_TABLE_LEN - KERNEL_STACK_PAGES; free_iter < free_physical_pages - kernel_pages;
	// 	free_iter++) {
	// 	free_physical_pages[free_iter] = free_iter + kernel_pages;
	// }
	// free_pages_counter = free_iter;
	// TracePrintf(10, "done with page table\n");

	//initialize REG_PTR0 and REG_PTR1
	WriteRegister(REG_PTR0, (RCS421RegVal) page_table_region_0);
	WriteRegister(REG_PTR1, (RCS421RegVal) page_table_region_1);
	TracePrintf(9, "[initPageTable] DONE \n"); 
}

void enable_virtual_memory() {
	WriteRegister(REG_VM_ENABLE, 1);
	vm_enabled = 1;
}


/* Helper Routines */

void linkedList_add(int queueNumber, pcb *add) {
	TracePrintf(0,"linkedList_add\n");
	// add to delay queue
 	if (queueNumber == 0) {	


        if (delay_q_head == NULL) {
        	TracePrintf(0,"Delay Q is empty\n");
        	delay_q_head = add;
        	delay_q_tail = add;
        }

        else {
	        pcb *running_proc = delay_q_head;
	        pcb *next_proc = delay_q_head->nextProc;
	 		while (running_proc != NULL) {
		  		// case1: add to head
		 		if (add -> switchTime < running_proc -> switchTime) {
		 			add -> nextProc = running_proc;
		 			delay_q_head = add;
		 			return;
	 			}
	 			else {
	 				if (next_proc == NULL) {
	 					TracePrintf(0, "HH:delay_q_head pid is: %d\n ", delay_q_head->pid);
	 					TracePrintf(0, "HH;delay_q_tail pid is: %d\n ", delay_q_tail->pid);	 					
	 					// case2: only one delay head, add to end
	 					running_proc->nextProc = add;
	 					delay_q_tail = add;
	 					return;
	 				}
	 				// case3: insert between current and next process
	 				else if (add -> switchTime > delay_q_head -> switchTime && add -> switchTime < delay_q_head->nextProc->switchTime) {
	 					running_proc->nextProc = add;
	 					add->nextProc = next_proc;
	 					return;
	 				}
	 				// case4: increment the pointer and keep on searching
	 				else {
	 					running_proc = next_proc;
	 					//pcb *nextProcess = next_proc->nextProc;
	 					TracePrintf(0, "next_proc PID is %d\n ", next_proc->pid);
	 					if (next_proc->nextProc == NULL) {
	 						TracePrintf(0, "next_proc->nextProc is NULL %d\n ");
	 					} else {
	 						TracePrintf(0, "next_proc->nextProc has pid %d\n ", next_proc->nextProc->pid);
	 					}
	 					
	 					TracePrintf(0, "delay_q_head pid is: %d\n ", delay_q_head->pid);
	 					TracePrintf(0, "delay_q_tail pid is: %d\n ", delay_q_tail->pid);
	 					//TracePrintf(0, "next_proc->nextProc PID is %d\n ", nextProcess->pid);

	 					next_proc = next_proc->nextProc;
	 				}

	 			}			
	 		}        	
        } 
 	}

	// add to ready queue
	else if (queueNumber == 1) {
		if (ready_q_head == NULL) {
			ready_q_head = add;
		} else {
			// add to end of ready queue
			ready_q_tail -> nextProc = add;
		}
		ready_q_tail = add;
	}

	// add to sibling queue
	else if (queueNumber == 2) {
		if (current_pcb->sibling_q_head == NULL) {
			current_pcb->sibling_q_head = add;
		} else {
			// add to end of ready queue
			current_pcb->sibling_q_tail -> nextProc = add;
		}
		current_pcb->sibling_q_tail = add;
	}

	// add to BlockQ_0
	else if (queueNumber == BlockQ_0) {
		if (Read_BlockQ_0_head == NULL) {
			Read_BlockQ_0_head = add;
		} else {
			// add to end of ready queue
			Read_BlockQ_0_tail -> nextProc = add;
		}
		Read_BlockQ_0_tail = add;		
	}
	// add to BlockQ_2
	else if (queueNumber == BlockQ_2) {
		if (Read_BlockQ_2_head == NULL) {
			Read_BlockQ_2_head = add;
		} else {
			// add to end of ready queue
			Read_BlockQ_2_tail -> nextProc = add;
		}
		Read_BlockQ_2_tail = add;		
	}

	// add to BlockQ_1
	else if (queueNumber == BlockQ_1) {
		if (Read_BlockQ_1_head == NULL) {
			Read_BlockQ_1_head = add;
		} else {
			// add to end of ready queue
			Read_BlockQ_1_tail -> nextProc = add;
		}
		Read_BlockQ_1_tail = add;		
	}

	// add to BlockQ_3
	else if (queueNumber == BlockQ_3) {
		if (Read_BlockQ_3_head == NULL) {
			Read_BlockQ_3_head = add;
		} else {
			// add to end of ready queue
			Read_BlockQ_3_tail -> nextProc = add;
		}
		Read_BlockQ_3_tail = add;		
	}

	// add to Write_BlockQ_0
	else if (queueNumber == Write_BlockQ_0) {
		if (Write_BlockQ_0_head == NULL) {
			Write_BlockQ_0_head = add;
		} else {
			// add to end of ready queue
			Write_BlockQ_0_tail -> nextProc = add;
		}
		Write_BlockQ_0_tail = add;		
	}	
	// add to Write_BlockQ_1
	else if (queueNumber == Write_BlockQ_1) {
		if (Write_BlockQ_1_head == NULL) {
			Write_BlockQ_1_head = add;
		} else {
			// add to end of ready queue
			Write_BlockQ_1_tail -> nextProc = add;
		}
		Write_BlockQ_1_tail = add;		
	}
	// add to Write_BlockQ_2
	else if (queueNumber == Write_BlockQ_2) {
		if (Write_BlockQ_2_head == NULL) {
			Write_BlockQ_2_head = add;
		} else {
			// add to end of ready queue
			Write_BlockQ_2_tail -> nextProc = add;
		}
		Write_BlockQ_2_tail = add;		
	}	
	// add to Write_BlockQ_3
	else if (queueNumber == Write_BlockQ_3) {
		if (Write_BlockQ_3_head == NULL) {
			Write_BlockQ_3_head = add;
		} else {
			// add to end of ready queue
			Write_BlockQ_3_tail -> nextProc = add;
		}
		Write_BlockQ_3_tail = add;		
	}			
	else {
		TracePrintf(0, "queueNumber%d\n", queueNumber);
		TracePrintf(0, "Error in add %d\n", current_pcb->pid);
	}
	TracePrintf(0,"[linkedList_add] DONE\n");

}

void *linkedList_remove(int queueNumber) {
	TracePrintf(0,"linkedList_remove \n");
	// remove from delay Q


	if (queueNumber == 0) {
		TracePrintf(0,"linkedList_remove Q = delay \n");

		if (delay_q_head == NULL) {
			return NULL;
		}		
		// case1: only 1 element in linked list
		else if (delay_q_head == delay_q_tail) {

			pcb *returnRn = delay_q_head;
			TracePrintf(0,"There is only 1 pcb in delay q, delay q head has pid %d\n", returnRn->pid);
			delay_q_head = delay_q_tail = NULL;
			return returnRn;
		}
		else {
			// case2: return the head
			pcb *returnRn = delay_q_head;
			TracePrintf(0,"There are MORE THAN 1 pcb in delay q, delay q head has pid %d\n", returnRn->pid);

			delay_q_head = delay_q_head->nextProc;
			returnRn->nextProc=NULL;
			return returnRn;
		}
	}

	//remove from sibling Q
	if (queueNumber == 2) {
		TracePrintf(0,"linkedList_remove Q = sibling \n");
		if (current_pcb->sibling_q_head == NULL) {
			return idle_pcb;
		}

		// case1: only 1 element in linked list
		if (current_pcb->sibling_q_head == current_pcb->sibling_q_tail) {
			pcb *returnRn = current_pcb->sibling_q_head;
			current_pcb->sibling_q_head = current_pcb->sibling_q_tail = NULL;
			return returnRn;
		}
		
		// case2: return the head
		pcb *returnRn = current_pcb->sibling_q_head;
		current_pcb->sibling_q_head = current_pcb->sibling_q_head->nextProc;
		return returnRn;		
	}

	if (queueNumber == BlockQ_0) {
		if (Read_BlockQ_0_head == NULL) {
			return NULL;
		} else if (Read_BlockQ_0_head == Read_BlockQ_0_tail) {
			pcb *returnRn = Read_BlockQ_0_head;
			Read_BlockQ_0_head = NULL;
			Read_BlockQ_0_tail = NULL;
			return returnRn;
		} else {
			pcb* returnRn = Read_BlockQ_0_head;
			Read_BlockQ_0_head = Read_BlockQ_0_head->nextProc;
			returnRn->nextProc = NULL;
			return returnRn;
		}
	}

	if (queueNumber == BlockQ_1) {
		if (Read_BlockQ_1_head == NULL) {
			return NULL;
		} else if (Read_BlockQ_1_head == Read_BlockQ_1_tail) {
			pcb *returnRn = Read_BlockQ_1_head;
			Read_BlockQ_1_head = NULL;
			Read_BlockQ_1_tail = NULL;
			return returnRn;
		} else {
			pcb* returnRn = Read_BlockQ_1_head;
			Read_BlockQ_1_head = Read_BlockQ_1_head->nextProc;
			returnRn->nextProc = NULL;
			return returnRn;
		}
	}
	if (queueNumber == BlockQ_2) {
		if (Read_BlockQ_2_head == NULL) {
			return NULL;
		} else if (Read_BlockQ_2_head == Read_BlockQ_2_tail) {
			pcb *returnRn = Read_BlockQ_2_head;
			Read_BlockQ_2_head = NULL;
			Read_BlockQ_2_tail = NULL;
			return returnRn;
		} else {
			pcb* returnRn = Read_BlockQ_2_head;
			Read_BlockQ_2_head = Read_BlockQ_2_head->nextProc;
			returnRn->nextProc = NULL;
			return returnRn;
		}
	}
	if (queueNumber == BlockQ_3) {
		if (Read_BlockQ_3_head == NULL) {
			return NULL;
		} else if (Read_BlockQ_3_head == Read_BlockQ_3_tail) {
			pcb *returnRn = Read_BlockQ_3_head;
			Read_BlockQ_3_head = NULL;
			Read_BlockQ_3_tail = NULL;
			return returnRn;
		} else {
			pcb* returnRn = Read_BlockQ_3_head;
			Read_BlockQ_3_head = Read_BlockQ_3_head->nextProc;
			returnRn->nextProc = NULL;
			return returnRn;
		}
	}

	if (queueNumber == Write_BlockQ_1) {
		if (Write_BlockQ_1_head == NULL) {
			return NULL;
		} else if (Write_BlockQ_1_head == Write_BlockQ_1_tail) {
			pcb *returnRn = Write_BlockQ_1_head;
			Write_BlockQ_1_head = NULL;
			Write_BlockQ_1_tail = NULL;
			return returnRn;
		} else {
			pcb* returnRn = Write_BlockQ_1_head;
			Write_BlockQ_1_head = Write_BlockQ_1_head->nextProc;
			returnRn->nextProc = NULL;
			return returnRn;
		}
	}

	if (queueNumber == Write_BlockQ_2) {
		if (Write_BlockQ_2_head == NULL) {
			return NULL;
		} else if (Write_BlockQ_2_head == Write_BlockQ_2_tail) {
			pcb *returnRn = Write_BlockQ_2_head;
			Write_BlockQ_2_head = NULL;
			Write_BlockQ_2_tail = NULL;
			return returnRn;
		} else {
			pcb* returnRn = Write_BlockQ_2_head;
			Write_BlockQ_2_head = Write_BlockQ_2_head->nextProc;
			returnRn->nextProc = NULL;
			return returnRn;
		}
	}
	if (queueNumber == Write_BlockQ_3) {
		if (Write_BlockQ_3_head == NULL) {
			return NULL;
		} else if (Write_BlockQ_3_head == Write_BlockQ_3_tail) {
			pcb *returnRn = Write_BlockQ_3_head;
			Write_BlockQ_3_head = NULL;
			Write_BlockQ_3_tail = NULL;
			return returnRn;
		} else {
			pcb* returnRn = Write_BlockQ_3_head;
			Write_BlockQ_3_head = Write_BlockQ_3_head->nextProc;
			returnRn->nextProc = NULL;
			return returnRn;
		}
	}

	if (queueNumber == Write_BlockQ_0) {
		if (Write_BlockQ_0_head == NULL) {
			return NULL;
		} else if (Write_BlockQ_0_head == Write_BlockQ_0_tail) {
			pcb *returnRn = Write_BlockQ_0_head;
			Write_BlockQ_0_head = NULL;
			Write_BlockQ_0_tail = NULL;
			return returnRn;
		} else {
			pcb* returnRn = Write_BlockQ_0_head;
			Write_BlockQ_0_head = Write_BlockQ_0_head->nextProc;
			returnRn->nextProc = NULL;
			return returnRn;
		}
	}

	// remove from ready Q
	if (ready_q_head == NULL) {
		TracePrintf(0,"linkedList_remove Q = NULL \n");
		return idle_pcb;
	}

	// case1: only 1 element in linked list
	if (ready_q_head == ready_q_tail) {
		TracePrintf(0,"linkedList_remove Q = 1 element \n");
		pcb *returnRn = ready_q_head;
		ready_q_head = ready_q_tail = NULL;
		return returnRn;
	}
	
	// case2: return the head
	pcb *returnRn = ready_q_head;
	ready_q_head = ready_q_head->nextProc;
	returnRn->nextProc=NULL;

	return (returnRn == NULL)?idle_pcb:returnRn;	

}

// returns the physical address of the new page, UNFINISHED!
int allocate_new_pfn() {
	int page_counter;
	for (page_counter = 0; page_counter < total_pages; page_counter++) {
		int page_index = (free_pages_pointer + page_counter) % total_pages;
		if (free_physical_pages[page_index] == 1) {
			free_pages_pointer = (page_index + 1) % total_pages;
			free_physical_pages[page_index] = 0;
			free_pages_counter--;
			return page_index;
		}
	}

	// 	for (page_counter = 0; page_counter < total_pages; page_counter++) {
	// 	int page_index = (free_pages_pointer + page_counter) % total_pages;
	// 	if (free_physical_pages[page_index] == 1) {
	// 		free_pages_pointer = page_index + 1;
	// 		free_physical_pages[page_index] = 0;
	// 		free_pages_counter--;
	// 		return page_index;
	// 	}
	// }
	TracePrintf(0, "No new pfn left!\n");
	return -1;
	//int index = free_physical_pages[--free_pages_counter];
	//return (void *)(index * PAGESIZE);
}

void deallocate_new_pfn(int pfn) {
	free_physical_pages[pfn] = 1;
	free_pages_counter++;
}


void print_free_list() {
	//TracePrintf(9, "%d\n", total_pages);
	int j;
	for (j = 0; j < total_pages; j++) {
		TracePrintf(9, "free list entry[%d]: %d\n", j, free_physical_pages[j]);
	}
}


// Custom context switch function
SavedContext *MySwitchFunc(SavedContext *ctxp, void *p1, void *p2) {
	pcb *pcb1 = (pcb *) p1;
	pcb *pcb2 = (pcb *) p2;
	TracePrintf(9, "Entering MySwitchFunc\n");
	// print_free_list();

	// case1: copy kernel stack if p1 and p2 are the same from the current kernel stack (e.g. copy init's to idle)
	if (pcb1 == pcb2) {
		TracePrintf(9, "MySwitchFunc case 1 start\n");
		// find a new physical page, copy 1 page from p1 kernel stack to that page, make 
		int stack_counter;
		//int empty_pfn;
		for (stack_counter = 0; stack_counter < 4; stack_counter++) {
			// find a new physical page, copy 1 page from p1 kernel stack to that page
			//TracePrintf(9, "MySwitchFunc allocate empty pfn start\n");
			int empty_pfn = allocate_new_pfn();
			//TracePrintf(9, "empty_pfn is %d\n", empty_pfn);
			//TracePrintf(9, "empty pfn: %d\n", empty_pfn);
			int empty_addr = empty_pfn * PAGESIZE;
			//TracePrintf(9, "MySwitchFunc allocate empty pfn ends\n");
			// map a free vpn to emtpy_pfn so that
			int free_vpn = current_pcb->brk_pos;
			// TracePrintf(0, "MySwitchFunc the free vpn is %d\n", free_vpn);
			// TracePrintf(9, "MySwitchFunc emtpy pfn starts\n");
			pt0[free_vpn].pfn = empty_pfn;
			//TracePrintf(9, "MySwitchFunc emtpy pfn ends\n");
			((struct pte*) pt0)[free_vpn].uprot = PROT_NONE;
			((struct pte*) pt0)[free_vpn].kprot = (PROT_READ | PROT_WRITE);
			((struct pte*) pt0)[free_vpn].valid = 1;
			WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_0);
			//TracePrintf(9, "MySwitchFunc memcpy start\n");
			// empty virtual addrr at vpn * PAGESIZE, copy this page to empty_pfn
			memcpy((void *) (free_vpn * PAGESIZE), (void *) (VMEM_0_LIMIT - (stack_counter + 1) * PAGESIZE), PAGESIZE);
			// TracePrintf(9, "Address of VPN 508 is %p\n",(VMEM_0_LIMIT - (stack_counter + 1) * PAGESIZE) );
			//TracePrintf(9, "MySwitchFunc memcpy done\n");

			// make free_vpn point to pcb1's physical pt0
			((struct pte*) pt0)[free_vpn].pfn = pcb1 -> pfn_pt0;
			WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_0);
			//TracePrintf(9, "pcb->pfn_pt0 %d\n", pcb1->pfn_pt0);


			// finally modify the kernel stack entry in pcb1's pt0
			((struct pte*) (free_vpn * PAGESIZE))[PAGE_TABLE_LEN - 1 - stack_counter].pfn = empty_pfn;
			((struct pte*) (free_vpn * PAGESIZE))[PAGE_TABLE_LEN - 1 - stack_counter].uprot = PROT_NONE;
			((struct pte*) (free_vpn * PAGESIZE))[PAGE_TABLE_LEN - 1 - stack_counter].kprot = (PROT_READ | PROT_WRITE);
			((struct pte*) (free_vpn * PAGESIZE))[PAGE_TABLE_LEN - 1 - stack_counter].valid = 1;

			WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_0);

			struct pte* addr = ((free_vpn<<PAGESHIFT)+(PAGE_TABLE_LEN - 1 - stack_counter)*(sizeof(struct pte)));
			//TracePrintf(9, "OLD VPN 508 has pfn %d\n", addr->pfn);
			// zero out this pt0 entry in the current process to be consistent
			((struct pte*) pt0)[free_vpn].valid = 0;
			WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_0);
			// ((struct pte*) pt0)[free_vpn].pfn = 250;


			TracePrintf(9, "mySwitchFunc case 1 done\n");
		}

		return &(pcb1 -> ctx);
	} else {
		if (pcb2 == NULL) {
			pcb2 = idle_pcb;
		}
		TracePrintf(9, "MySwitchFunc case 2 start\n");
	// case2: change register ptr0 to the new physical address of pt0 if p1 and p2 are different 
	// and modify pt1's second to last entry to point to the new pt0 
	// zero out the pt0 entry of the p2

		TracePrintf(9, "MySwitchFunc case 2, pcb2 has pid: %d\n", pcb2 -> pid);
	
		TracePrintf(9, "The old pt0 has pfn %d\n",page_table_region_1[PAGE_TABLE_LEN - 2].pfn);
		if (pcb1 -> status == TERMINATED) {
			TracePrintf(9, "MySwitchFunc case 2, pcb1 terminated freeing, pid: %d\n", pcb1 -> pid);
			int itr;
	        for (itr = MEM_INVALID_PAGES; itr < (VMEM_REGION_SIZE >> PAGESHIFT); itr++) {
	            if (pt0[itr].valid) deallocate_new_pfn(itr);
	        }

	        // free child exit infos
	        ecb *current = pcb1 -> exited_childq_head;
	        while (current != NULL) {
	        	ecb *next = current -> next;
	        	free(current);
	        	current = next;
	        }
	        // free pcb
	        free(pcb1);
	        
	        if (pcb2 == idle_pcb) {
	        	if (delay_q_head == NULL && ready_q_head == NULL && Read_BlockQ_0_head == NULL && Read_BlockQ_1_head == NULL && Read_BlockQ_2_head == NULL && Read_BlockQ_3_head == NULL) {
	        		Halt();
	        	
	 			}
	        }
		}


		

		//TracePrintf(9, "Old VPN 508 has valid bit %d\n", pt0[509].pfn);
		WriteRegister(REG_PTR0, (RCS421RegVal) (pcb2 -> pfn_pt0) << PAGESHIFT);
		page_table_region_1[PAGE_TABLE_LEN - 2].pfn = pcb2 -> pfn_pt0;

		WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_ALL);
		current_pcb = pcb2;
		// TracePrintf(9, "The new pcb has pfn %d\n", );
		// TracePrintf(9, "The current process has pid %d\n", current_pcb->pid);
		
		// TracePrintf(9, "New VPN 508 has valid bit %d\n", pt0[509].pfn);

		// just testing, comment this out afterwards

		// int free_vpn = (UP_TO_PAGE(region0_brk)) >> PAGESHIFT;
		
		// ((struct pte*) pt0)[free_vpn].pfn = 0;
		// WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_0);
		// struct pte* addr = ((free_vpn<<PAGESHIFT)+508*(sizeof(struct pte)));
		// TracePrintf(9, "New VPN 508 has pfn %d\n", addr->pfn);
		TracePrintf(9, "mySwitchFunc case 2 done\n");
		return &(pcb2 -> ctx);
	}
}

// make pcb
pcb *make_pcb(int pfn, int pid) {

    pcb *process = malloc(sizeof(pcb));
    if (process == NULL) {
        fprintf(stderr, "PCB malloc failed\n");
        return NULL;
    }

    // process->ctx = calloc(1, sizeof(SavedContext));

    // if (process->ctx == NULL) {
    //     fprintf(stderr, "new program ctx malloc failed\n");
    //     return NULL;
    // }

	process->pid = pid;
	process->status = READY; // NEW
	process->pfn_pt0 = pfn;
	process->nextProc = NULL;
	process->switchTime = total_time + 2;
    process->brk_pos = region0_brk;
    process->user_stack_low_vpn = current_pcb->user_stack_low_vpn;
    process->child = NULL;
    process->parent = pid>1 ? current_pcb : NULL;	// NEW
    process->next_child = NULL;					// NEW
  	process->sibling_q_head = NULL;
	process->sibling_q_tail = NULL; 
	process->exited_childq_head = NULL;			// NEW
	process->exited_childq_tail = NULL;			// NEW
    ContextSwitch(MySwitchFunc, &process->ctx, process, process);

  
    TracePrintf(0,"[make_pcb] DONE\n");
    return process;
}


/* END of Kernel Start Sub-Routines */

/************************************DAT WALL************************************/

/* Trap Handlers */
// Section 3.2
void trap_kernel(ExceptionInfo *info) {
 TracePrintf(0,"Entering trap kernel\n");
 /* get which kind of kernel call we have */
 int call_type = info -> code;

 if (call_type == YALNIX_FORK) {
     TracePrintf(0, "Fork()\n");
     info->regs[0] = Fork();
     //Halt();
     return;
 }
 else if (call_type == YALNIX_EXEC) {
     TracePrintf(0, "Exec()\n");
     info->regs[0] = ExecFunc((char *)(info->regs[1]), (char **)(info->regs[2]), info);
     //Halt();
     return;
 }
 else if (call_type == YALNIX_EXIT) {
     TracePrintf(0, "Exit()\n");
     Exit((int)info->regs[1]);
     //Halt();
     return;
 }
 else if (call_type == YALNIX_WAIT) {
     TracePrintf(0, "Wait()\n");
     info->regs[0] = Wait(info->regs[1]);
     //Halt();
     return;
 }
 else if (call_type == YALNIX_GETPID) {
     TracePrintf(0, "GetPid()\n");
     info->regs[0] = GetPid();
     return;
 }
 else if (call_type == YALNIX_BRK) {
     TracePrintf(0, "Brk()\n");
     info->regs[0] = Brk((void *)info->regs[1]);
     //Halt();
     return;
 }
 else if (call_type == YALNIX_DELAY) {
     TracePrintf(0, "Delay()\n");
     info->regs[0] = Delay((int)info->regs[1]);
     return; 
 }
 else if (call_type == YALNIX_TTY_READ) {
     TracePrintf(0, "TTY_READ()\n");
     info->regs[0] = TtyRead(info->regs[1], info->regs[2], info->regs[3]);
     //Halt();
     return; 
 }
 else if (call_type == YALNIX_TTY_WRITE) {
     TracePrintf(0, "TTY_WRITE()\n");
     info->regs[0] = TtyWrite((int)(info->regs[1]), (void *)(info->regs[2]), (int)(info->regs[3]));
     //Halt();
     return;
 }
 else {
  TracePrintf(0, "Code not found: Just break\n");
  return;
	}   
}



void trap_clock(ExceptionInfo *info) {
	TracePrintf(0, "trap_clock runs\n");
	total_time++;
	TracePrintf(0, "Time is: %d\n", total_time);

	// Function1: move process from delay queue to ready queue
	//if processes reach time to switch, and delay queue now empty, move from delay Q to ready Q
	if (delay_q_head != NULL) {
		TracePrintf(0, "delay_q_head is %d\n", delay_q_head->pid);
		TracePrintf(0, "123\n");
		TracePrintf(0, "delay_q_head ST is %d\n", delay_q_head->switchTime);
	}
	
	// TracePrintf(0, "delay_q_head ST is %s\n", delay_q_head->switchTime);
    if (delay_q_head != NULL && delay_q_head->switchTime <= total_time) {
    	//TracePrintf(0, "HERE33");
    	pcb *temp = linkedList_remove(0);
    	TracePrintf(0, "delay_q_head has pid %d\n", temp->pid);
        linkedList_add(1, temp);
		TracePrintf(0, "ready_q_head has pid %d", ready_q_head->pid);    }
    //TracePrintf(0, "HERE44");

    // Function2: contextSwitch process on ready queue
	if (current_pcb == idle_pcb || current_pcb->switchTime == total_time) {
		//TracePrintf(0, "Bill\n");
		// TracePrintf(0, "ready_q_head has pid %d", ready_q_head->pid);
		if (ready_q_head != NULL) {
			// TracePrintf(0, "HEREX11\n");
			//TracePrintf(0, "Curreent process is %d\n", current_pcb->pid);
			if (current_pcb == idle_pcb) {
				//TracePrintf(0, "Current process is idle, switching to pid %d\n", ready_q_head->pid);
				ContextSwitch(MySwitchFunc, &current_pcb->ctx, (void *)current_pcb, (void *)linkedList_remove(1));
			} 
			else {
			    //TracePrintf(0, "Context Switch for pid %d\n", current_pcb->pid);
			    linkedList_add(1, current_pcb);
			    ContextSwitch(MySwitchFunc, &current_pcb->ctx, (void *)current_pcb, (void *)linkedList_remove(1));				
			}

		} else {
			if (current_pcb != idle_pcb) {
				ContextSwitch(MySwitchFunc, &current_pcb->ctx, (void*) current_pcb, (void*) idle_pcb);
			}
		}
	}   
};

void trap_illegal(ExceptionInfo *info) {
	/* get the type for illegal */
	int illegal_type = info->code;
	char *text;

	if (illegal_type == TRAP_ILLEGAL_ILLOPC) {
		/* Terminate the currently running Yalnix user process */
		//Pause();
		/* print error msg based on PID */
		//TracePrintf(1, "Illegal opcode, PID is%d\n", GetPid());
		fprintf(stderr, "Illegal opcode, PID is%d\n", GetPid());
	}

	if (illegal_type == TRAP_ILLEGAL_ILLOPN) {
		/* Terminate the currently running Yalnix user process */
		//Pause();
		/* print error msg based on PID */
		// TracePrintf(1, "Illegal operand, PID is%d\n", GetPid());
		fprintf(stderr, "Illegal operand, PID is%d\n", GetPid());
	}

	if (illegal_type == TRAP_ILLEGAL_ILLADR) {
		/* Terminate the currently running Yalnix user process */
		//Pause();
		/* print error msg based on PID */
		// TracePrintf(1, "Illegal addressing mode, PID is%d\n", GetPid());
		fprintf(stderr, "Illegal addressing mode, PID is%d\n", GetPid());
	}
	if (illegal_type == TRAP_ILLEGAL_ILLTRP) {
		/* Terminate the currently running Yalnix user process */
		//Pause();
		/* print error msg based on PID */
		// TracePrintf(1, "Illegal software trap, PID is%d\n", GetPid());
		fprintf(stderr,  "Illegal software trap, PID is%d\n", GetPid());
	}
	if (illegal_type == TRAP_ILLEGAL_PRVOPC) {
		/* Terminate the currently running Yalnix user process */
		//Pause();
		/* print error msg based on PID */
		// TracePrintf(1, "Privileged opcode, PID is%d\n", GetPid());
		fprintf(stderr, "Privileged opcode, PID is%d\n", GetPid());
	}
	if (illegal_type == TRAP_ILLEGAL_PRVREG) {
		/* Terminate the currently running Yalnix user process */
		//Pause();
		/* print error msg based on PID */
		// TracePrintf(1, "Privileged register, PID is%d\n", GetPid());
		fprintf(stderr, "Privileged register, PID is%d\n", GetPid());
	}
	if (illegal_type == TRAP_ILLEGAL_COPROC) {
		/* Terminate the currently running Yalnix user process */
		//Pause();
		/* print error msg based on PID */
		// TracePrintf(1, "Coprocessor error, PID is%d\n", GetPid());
		fprintf(stderr, "Coprocessor error, PID is%d\n", GetPid());
	}
	if (illegal_type == TRAP_ILLEGAL_BADSTK) {
		/* Terminate the currently running Yalnix user process */
		//Pause();
		/* print error msg based on PID */
		// TracePrintf(1, "Bad stack, PID is%d\n", GetPid());
		fprintf(stderr, "Bad stack, PID is%d\n", GetPid());
	}
	if (illegal_type == TRAP_ILLEGAL_KERNELI) {
		/* Terminate the currently running Yalnix user process */
		//Pause();
		/* print error msg based on PID */
		// TracePrintf(1, "Linux kernel sent SIGILL, PID is%d\n", GetPid());
		fprintf(stderr, "Linux kernel sent SIGILL, PID is%d\n", GetPid());
	}
	if (illegal_type == TRAP_ILLEGAL_USERIB) {
		/* Terminate the currently running Yalnix user process */
		//Pause();
		/* print error msg based on PID */
		// TracePrintf(1, "Received SIGILL or SIGBUS from user, PID is%d\n", GetPid());
		fprintf(stderr, "Received SIGILL or SIGBUS from user, PID is%d\n", GetPid());
	}
	if (illegal_type == TRAP_ILLEGAL_ADRALN) {
		/* Terminate the currently running Yalnix user process */
		//Pause();
		/* print error msg based on PID */
		// TracePrintf(1, "Invalid address alignment, PID is%d\n", GetPid());
		fprintf(stderr, "Invalid address alignment, PID is%d\n", GetPid());
	}
	if (illegal_type == TRAP_ILLEGAL_ADRERR) {
		/* Terminate the currently running Yalnix user process */
		//Pause();
		/* print error msg based on PID */
		// TracePrintf(1, "Non-existent physical address, PID is%d\n", GetPid());
		fprintf(stderr, "Non-existent physical address, PID is%d\n", GetPid());
	}
	if (illegal_type == TRAP_ILLEGAL_OBJERR) {
		/* Terminate the currently running Yalnix user process */
		//Pause();
		/* print error msg based on PID */
		// TracePrintf(1, "Object-specific HW error, PID is%d\n", GetPid());
		fprintf(stderr, "Object-specific HW error, PID is%d\n", GetPid());
	}
	if (illegal_type == TRAP_ILLEGAL_KERNELB) {
		/* Terminate the currently running Yalnix user process */
		//Pause();
		/* print error msg based on PID */
		// TracePrintf(1, "Linux kernel sent SIGBUS, PID is%d\n", GetPid());
		fprintf(stderr, "Linux kernel sent SIGBUS, PID is%d\n", GetPid());
	}
	TracePrintf(0,"Entered TRAP \n");
	Exit(ERROR);
}

void trap_memory(ExceptionInfo *info) {
	TracePrintf(0,"Entered TRAP MEMORY\n");
	TracePrintf(0, "Address for TRAP MEMORY is %p\n", info->addr);
	TracePrintf(0, "Page is %d\n", (int)UP_TO_PAGE(info->addr)>>PAGESHIFT);
	TracePrintf(0, "Code for TRAP MEMORY is %p\n", info->code);
	//Halt();

	// Print error msg
	int memory_trap_type = info->code;

	if (memory_trap_type == TRAP_MEMORY_MAPERR) {
		fprintf(stderr, "TRAP_MEMORY_MAPERR %s\n");
	}
	else if (memory_trap_type == TRAP_MEMORY_ACCERR) {
		fprintf(stderr, "TRAP_MEMORY_ACCERR %s\n");
	} 
	else if (memory_trap_type == TRAP_MEMORY_KERNEL) {
		fprintf(stderr, "TRAP_MEMORY_KERNEL %s\n");	
	}
	else if (memory_trap_type == TRAP_MEMORY_USER) {
		fprintf(stderr, "TRAP_MEMORY_USER %s\n");			
	}
	else {
		fprintf(stderr, "ERROR in trap_memory\n");
	}


	TracePrintf(0,"BEFORE\n");
	void *this_addr = info->addr;
	int this_vpn = (long)DOWN_TO_PAGE(this_addr) >> PAGESHIFT;
	int brk_pos_vpn = current_pcb->brk_pos;
	//int user_stack_low_vpn = (long)DOWN_TO_PAGE(current_pcb->user_stack_low_addr) >> PAGESHIFT;
	TracePrintf(0,"AFTER\n");

	if (this_vpn < current_pcb->user_stack_low_vpn && this_vpn > (brk_pos_vpn + 1)) {
		TracePrintf(0,"Entered IF: we grow \n");
		int start = current_pcb->user_stack_low_vpn;
		TracePrintf(0, "First addr of user stack is %d\n", start);
		int end = (long)DOWN_TO_PAGE((long)this_addr) >> PAGESHIFT;
		int i;
		for (i = start - 1; i >= end; i--) {
			pt0[i].valid = 1;
			pt0[i].kprot = (PROT_READ | PROT_WRITE);
			pt0[i].uprot = (PROT_READ | PROT_WRITE);
			pt0[i].pfn = allocate_new_pfn();
		}
		WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_0);
		current_pcb->user_stack_low_vpn = this_vpn;
		
	} 

	else {
		TracePrintf(0,"MDMDM\n");
		Exit(ERROR);
		TracePrintf(0,"Exit ERROR\n");
        // pcb *next = linkedList_remove(1);
        // ContextSwitch(MySwitchFunc, &current_pcb->ctx, (void *)current_pcb, (void *) next);			
		Halt();
	}



}

void trap_math(ExceptionInfo *info) {
	TracePrintf(0, "Entering TRAP MATH\n");
	char* text;

	/* get the type of math trap */
	int trap_math_type = info->code;

	if (trap_math_type == TRAP_MATH_INTDIV) {
		/* Terminate the currently running Yalnix user process */
		//Halt();
		/* print error msg based on PID */
		// TracePrintf(1, "integer divide by zero");
		text = "integer divide by zero";		
	}
	if (trap_math_type == TRAP_MATH_INTOVF) {
		/* Terminate the currently running Yalnix user process */
		//Halt();
		/* print error msg based on PID */
		// TracePrintf(1, "Integer overflow");
		text = "Integer overflow";		
	}
	if (trap_math_type == TRAP_MATH_FLTDIV) {
		/* Terminate the currently running Yalnix user process */
		//Halt();
		/* print error msg based on PID */
		// TracePrintf(1, "Floating divide by zero");	
		text = "Floating divide by zero";	
	}
	if (trap_math_type == TRAP_MATH_FLTOVF) {
		/* Terminate the currently running Yalnix user process */
		//Halt();
		/* print error msg based on PID */
		// TracePrintf(1, "Floating overflow");
		text = "Floating overflow";		
	}
	if (trap_math_type == TRAP_MATH_FLTUND) {
		/* Terminate the currently running Yalnix user process */
		//Halt();
		/* print error msg based on PID */
		// TracePrintf(1, "Floating underflow");
		text = "Floating underflow";		
	}
	if (trap_math_type == TRAP_MATH_FLTRES) {
		/* Terminate the currently running Yalnix user process */
		//Halt();
		/* print error msg based on PID */
		// TracePrintf(1, "Floating inexact result");	
		text = "Floating inexact result";	
	}
	if (trap_math_type == TRAP_MATH_FLTINV) {
		/* Terminate the currently running Yalnix user process */
		//Halt();
		/* print error msg based on PID */
		// TracePrintf(1, "Invalid floating operation");
		text = "Invalid floating operation";
	}
	if (trap_math_type == TRAP_MATH_FLTSUB) {
		/* Terminate the currently running Yalnix user process */
		//Halt();
		/* print error msg based on PID */
		// TracePrintf(1, "FP subscript out of range");
		text = "FP subscript out of range";		
	}
	if (trap_math_type == TRAP_MATH_KERNEL) {
		/* Terminate the currently running Yalnix user process */
		//Halt();
		/* print error msg based on PID */
		// TracePrintf(1, "Linux kernel sent SIGFPE");
		text = "Linux kernel sent SIGFPE";		
	}
	if (trap_math_type == TRAP_MATH_USER) {
		/* Terminate the currently running Yalnix user process */
		//Halt();
		/* print error msg based on PID */
		// TracePrintf(1, "Received SIGFPE from user");
		text = "Received SIGFPE from user";		
	}
	fprintf(stderr, "TRAP_MATH: %s\n", text);
	//ContextSwitch(MySwitchFunc, current_pcb -> ctx, (void *) current_pcb, (void *)linkedList_remove(ReadyQueue));

    Exit(ERROR);
}




void lineList_add(int term, Line *line_to_add) {


	if (terminal[term]->readterm_lineList_head == NULL && terminal[term]->readterm_lineList_tail == NULL) {
		TracePrintf(0, "Line head is null\n");
		TracePrintf(0, "Line to add has length %d\n", line_to_add->length);
		terminal[term]->readterm_lineList_head = line_to_add;
		TracePrintf(0, "Length of head is %d\n", terminal[term]->readterm_lineList_head->length);
		terminal[term]->readterm_lineList_tail = line_to_add;
	}
	else {
		TracePrintf(0, "Line head is NOT NULL\n");
		terminal[term]->readterm_lineList_tail->nextLine = line_to_add;
		terminal[term]->readterm_lineList_tail = line_to_add;		
	}


}
	

pcb *whichHead(int term) {
	pcb *BlockQ_head;
    	if (term == 0) {
    		BlockQ_head = Read_BlockQ_0_head;
    	} else if (term == 1) {
    		BlockQ_head = Read_BlockQ_1_head;
    	} else if (term == 2) {
    		BlockQ_head = Read_BlockQ_2_head;
    	} else if (term == 3) {
    		BlockQ_head = Read_BlockQ_3_head;
    	}
    return BlockQ_head;
}

// Line *whichHeadLine(int term) {
// 	Line *BlockQ_head;
    	
//     return BlockQ_head;
// }




void trap_tty_receive(ExceptionInfo *info) {
	TracePrintf(1, "HERE11 \n");
	//when the user types a line, we enter this trap
	int term_num = info->code;
	Terminal *current_terminal = terminal[term_num];
	Line *line_list_head;
	int BlockQ_number = term_num+3;

	TracePrintf(1, "HERE22 \n");

	int addition_char;
	//copy to terminal 0 Read Buffer
	char *buf = (char*) malloc(TERMINAL_MAX_LINE * sizeof(char));
	//buf = current_terminal->BufRead + current_terminal->num_char;
	addition_char = TtyReceive(term_num, buf, TERMINAL_MAX_LINE);
    //update number of chars can be read
    current_terminal->num_char += addition_char;
    // build a new line struct for the newly read line
    Line *new_line = malloc(sizeof(Line));
    new_line->ReadBuf = buf;
    new_line->length = addition_char;
    new_line->nextLine = NULL;

    if (new_line == NULL) {
    	TracePrintf(0, "NEW LINE IS NULLLLLLL\n");
    }

    //add to line linked list 
    lineList_add(term_num,new_line);

    Line *first = (terminal[term_num]->readterm_lineList_head);

    if (terminal[term_num]->readterm_lineList_head == NULL) {
    	TracePrintf(0, "First IS NULLLLLLL\n");
    }    
    //TracePrintf (0, "Head of line list has length %d\n", first->length);


    TracePrintf(1, "HERE33 \n");

    if (current_pcb == idle_pcb && addition_char > 0 && whichHead(info->code) != NULL) {
    	TracePrintf(1, "current_pcb == idle_pcb && addition_char > 0 \n");

    	pcb* BlockQ_head = whichHead(info->code);

    	pcb *next_on_blockq = BlockQ_head;
    	TracePrintf(0, "Head of terminal has pid %d\n", next_on_blockq->pid);
    	BlockQ_head = next_on_blockq->nextProc;
    	ContextSwitch(MySwitchFunc, &current_pcb->ctx, (void *)current_pcb, (void*)next_on_blockq);
    } 
    if (ready_q_head != NULL) {
    	TracePrintf(1, "ready_q_head != NULL \n");
    	ContextSwitch(MySwitchFunc, &current_pcb->ctx, (void *)current_pcb, (void *)linkedList_remove(ReadyQueue));
    }    
    TracePrintf(1, "[trap_tty_receive] DONE \n");
}

extern int TtyRead(int tty_id, void *buf, int len) {

	TracePrintf(0, "Len is %d\n", len);

	if (buf == NULL || len < 0) {
		return ERROR;
	}
	TracePrintf(1, "HERE1 \n");
	int BlockQ_number;
	BlockQ_number = tty_id+3;
	int result;

    // If nothing to read, add current pcb to block queue
    if (terminal[tty_id]->num_char == 0) {
    	TracePrintf(1, "nothing to read, add current pcb to block queue \n");
    	linkedList_add(BlockQ_number, current_pcb);
    	pcb* BlockQ_head = whichHead(tty_id);
    	TracePrintf(0, "After adding, head of block Q has pid %d\n", BlockQ_head->pid);
    	if (ready_q_head != NULL) {
    		ContextSwitch(MySwitchFunc, &current_pcb->ctx, (void *)current_pcb, (void *)linkedList_remove(ReadyQueue));    		
    	} 
    	else {
 			ContextSwitch(MySwitchFunc, &current_pcb->ctx, (void *)current_pcb, (void *)idle_pcb);   		
    	}
    	
    }
    TracePrintf(1, "HERE2 \n");
	int char_left_to_read;

	//char_left_to_read = terminal[tty_id]->readterm_lineList_head->length;
	
	if (terminal[tty_id]->readterm_lineList_head != NULL) {
		// if (whichHead(tty_id) == NULL) {
		// 	break;
		// }
		TracePrintf(1, "Enter while loop \n");
		Line *current_line_is = terminal[tty_id]->readterm_lineList_head;

		TracePrintf(0,"The current line length is %d\n", current_line_is->length);
		char_left_to_read = current_line_is->length;

	    //copy to buf when len want to read is less than the chars in the line's Read buffer
	    if (len <= current_line_is->length) {
	    	TracePrintf(1, "current line has MORE characters than we can read \n");
	    	memcpy(buf, current_line_is->ReadBuf, len);
	    	memcpy(current_line_is->ReadBuf, current_line_is->ReadBuf + len, (current_line_is->length)-len);
	    	(current_line_is->length) -= len;
	    	//char_left_to_read = ;
	    	terminal[tty_id]->num_char -=len;

	    	// if we read the whole line, update the line linked list
	    	if (current_line_is->length <= 0) {
	    		TracePrintf(1, "read the whole line, update the line linked list \n");
	    		terminal[tty_id]->readterm_lineList_head = current_line_is->nextLine;
	    	}
	    	result = len;
	    } 
	    // if we want to read more than what's inside the one line's read buf, look at the next line
	    else {
	    	TracePrintf(1, "current line has LESS characters than we can read \n");
	    	int current_line_length;
	    	current_line_length = current_line_is->length;
    		memcpy(buf, current_line_is->ReadBuf, current_line_length);
    		result = current_line_is->length;
    		(current_line_is->length) = 0;
    		terminal[tty_id]->num_char -= current_line_length;
    		char_left_to_read = 0;
    		

    		//go to the next line
    		if (terminal[tty_id]->readterm_lineList_head == terminal[tty_id]->readterm_lineList_tail) {
    			terminal[tty_id]->readterm_lineList_head = NULL;
    			terminal[tty_id]->readterm_lineList_tail = NULL;
    		} else {
    			Line *temp = terminal[tty_id]->readterm_lineList_head;
    			terminal[tty_id]->readterm_lineList_head = terminal[tty_id]->readterm_lineList_head->nextLine;
    			temp->nextLine = NULL;
    		}
    		if (terminal[tty_id]->readterm_lineList_head != NULL)
    			TracePrintf(0, "Finished reading a line, next line has length %d\n", terminal[tty_id]->readterm_lineList_head->length);
	    }
	    
	}
	linkedList_remove(BlockQ_number);
	TracePrintf(1, "HERE3 \n");



	// Switch to next process after read done.
    // if (ready_q_head != NULL) {
    // 	TracePrintf(1, "Switch to next process after read done. \n");
    //    ContextSwitch(MySwitchFunc, &current_pcb->ctx, (void *)current_pcb, (void *)linkedList_remove(ReadyQueue));
    // }
    TracePrintf(1, "HERE4 \n");
	return result;



}


/* Kernel Call Implementation */



extern int Delay(int clock_ticks) {
	TracePrintf(0, "(Delay) Total time is %d\n", total_time);

    TracePrintf(0, "<Delay> pid %d\n", current_pcb->pid);
    if (clock_ticks < 0) {
    	return ERROR;
    }

    if (clock_ticks == 0) {
    	return 0;
    }
    if (ready_q_head == NULL) {
    	//TracePrintf(0, "HERE1");
    	current_pcb->switchTime = total_time + clock_ticks;
    	linkedList_add(0, current_pcb);
    	TracePrintf(0, "First pcb in delay q has pid %d\n", delay_q_head->pid);
    	ContextSwitch(MySwitchFunc, &current_pcb->ctx, (void *)current_pcb, (void *)idle_pcb);
    	//TracePrintf(0, "HERE2\n");
    }

    else {
	    current_pcb->switchTime = total_time + clock_ticks;
	    
	    linkedList_add(0, current_pcb);

	    ContextSwitch(MySwitchFunc, &current_pcb->ctx, (void *)current_pcb, (void *)linkedList_remove(1));    	
    }
    TracePrintf(0, "<Delay> pid end: %d\n", current_pcb->pid);
    TracePrintf(0, "[Delay] DONE \n");
    return 0;
}

int GetPid(void) {
	return current_pcb -> pid;
}

extern int Brk(void *addr) {
	TracePrintf(0, "pid %d\n set <Brk>", current_pcb->pid);
	int set_brk = UP_TO_PAGE((int)addr) >> PAGESHIFT;
	void *current_pcb_brk_addr = (current_pcb->brk_pos) << PAGESHIFT;

	if (current_pcb->brk_pos > current_pcb->user_stack_low_vpn - 1) {
		TracePrintf(0, "current process brk addr is bigger than user_stack_low_addr - 1 page");
		return ERROR;
	}

	// if (current_pcb->user_stack_low_addr > VMEM_0_LIMIT - 5*PAGESIZE) {
	// 	TracePrintf(0, "user_stack_low_addr is higher than VMEM_0_LIMIT - 5*PAGESIZE\n");
	// 	return ERROR;
	// }

	// case1: set_brk bigger than brk_pos, move up 
	if (set_brk > current_pcb->brk_pos && set_brk <= (current_pcb->user_stack_low_vpn-1)) {
		TracePrintf(0, "Brk Case1\n ");
		int current_pcb_vpn = current_pcb->brk_pos;
		int num_pages_up = (set_brk - current_pcb_vpn);
		int i;
		for (i = 0; i < num_pages_up; i++) {
			//enqueue a page
			pt0[current_pcb->brk_pos + i].pfn = allocate_new_pfn();
			pt0[current_pcb->brk_pos + i].valid = 1;
			pt0[current_pcb->brk_pos + i].uprot = (PROT_READ | PROT_WRITE);
			pt0[current_pcb->brk_pos + i].kprot = (PROT_READ | PROT_WRITE);
			WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_0);

		}
		current_pcb->brk_pos = set_brk;
	} 
	// case2: set_brk smaller than brk_pos, move down 
	else if (set_brk < current_pcb->brk_pos && set_brk >= MEM_INVALID_PAGES) {
		TracePrintf(0, "Brk Case2\n ");
		//int current_pcb_vpn = current_pcb->brk_pos >> PAGESHIFT;
		int num_pages_down = (current_pcb->brk_pos - set_brk);
		int i;
		for (i = 0; i < num_pages_down; i++) {
			//dequeue a page
			deallocate_new_pfn(pt0[current_pcb->brk_pos+i].pfn);
			pt0[current_pcb->brk_pos+i].valid = 0;
			WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_0);
		}		
		current_pcb->brk_pos = set_brk;
	}
	else {
		TracePrintf(0, "Brk ERROR\n ");
		return ERROR;
	}
	TracePrintf(0, "<Brk> end: pid %d\n", current_pcb->pid);
	TracePrintf(0, "[Brk] DONE \n");
}



int Fork(void) {
	TracePrintf(0, "<Fork> for pid: %d\n", current_pcb->pid);
	// Step1: new page Table, return a new pfn for this pt0
	int *new_pt0 = allocate_new_pfn();

	// Step2: Create a new process
	//TracePrintf(9, "process_id is: %d\n", process_id);
	int new_pid = process_id++;

	int stack_counter;
	//TracePrintf(9, "NewPID is: %d\n", new_pid);
	//void *pt0_physical_addr = new_pt0 * PAGESIZE;
	pcb *new_processPCB = make_pcb(new_pt0, new_pid);
	new_processPCB->brk_pos = current_pcb->brk_pos;

	//TracePrintf(0, "VPN 1 of current process has valid bit %d\n", pt0[1].valid);

	pcb *pcb1 = new_processPCB;


	
	// Step3: if child return 0
	if (current_pcb == new_processPCB) {
		TracePrintf(0, "<Fork> child return, pid is: %d\n", current_pcb->pid);
		
		// int i;
		// for (i=16; i <=26; i++) {
		// 	TracePrintf(0, "child stack_counter is: %d\n", i);
		// 	TracePrintf(0, "child stack_counter Valid Bit is: %d\n", pt0[i].valid);
		// }
		// TracePrintf(0, "child stack_counter Valid Bit is: %d\n", pt0[506].valid);
		return 0;
	} else {
		//Step4: If parent, Copy pages and ContextSwitch

		pcb *pcb1 = new_processPCB;

		//int stack_counter;
		int last_page = PAGE_TABLE_LEN - KERNEL_STACK_PAGES;
		for (stack_counter = MEM_INVALID_PAGES; stack_counter < last_page; stack_counter++) {
			if (pt0[stack_counter].valid == 1) {
				//TracePrintf(9, "stack_counter is: %d\n", stack_counter);
				//TracePrintf(9, "Fork allocate empty pfn start\n");
				int empty_pfn = allocate_new_pfn();
				// TracePrintf(9, "empty_pfn is %d\n", empty_pfn);
				// TracePrintf(9, "empty pfn: %d\n", empty_pfn);
				int empty_addr = empty_pfn * PAGESIZE;
				//TracePrintf(9, "Fork allocate empty pfn ends\n");
				
				// map a free vpn to emtpy_pfn so that
				// int free_vpn = 150;
				int free_vpn = current_pcb->brk_pos;
				//TracePrintf(9, "VPN to copy is %d\n", stack_counter);
				TracePrintf(9, "Free vpn is %d\n", free_vpn);

				pt0[free_vpn].pfn = empty_pfn;
				//TracePrintf(9, "Fork emtpy pfn ends\n");
				((struct pte*) pt0)[free_vpn].uprot = (PROT_READ | PROT_EXEC);
				((struct pte*) pt0)[free_vpn].kprot = (PROT_READ | PROT_WRITE);
				((struct pte*) pt0)[free_vpn].valid = 1;
				WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_0);
				//TracePrintf(9, "Forkmemcpy start\n");
				// empty virtual addrr at vpn * PAGESIZE, copy this page to empty_pfn
				memcpy((void *) (free_vpn * PAGESIZE), (void *) (stack_counter * PAGESIZE), PAGESIZE);
				// TracePrintf(9, "Address of VPN 508 is %p\n",(VMEM_0_LIMIT - (stack_counter + 1) * PAGESIZE) );
				//TracePrintf(9, "Fork memcpy done\n");

				// make free_vpn point to pcb1's physical pt0
				((struct pte*) pt0)[free_vpn].pfn = pcb1 -> pfn_pt0;
				//TracePrintf(9, "pcb->pfn_pt0 %d\n", pcb1->pfn_pt0);

				// Now we can modify new pt0 by accessing free_vpn

				WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_0);
				//TracePrintf(9, "pcb->pfn_pt0 %d\n", pcb1->pfn_pt0);



				// finally modify the kernel stack entry in pcb1's pt0
			    ((struct pte*) (free_vpn * PAGESIZE))[stack_counter].pfn = empty_pfn;
			    // ((struct pte*) (free_vpn * PAGESIZE))[stack_counter].uprot = (PROT_READ | PROT_EXEC);
			    // ((struct pte*) (free_vpn * PAGESIZE))[stack_counter].kprot = (PROT_READ | PROT_WRITE);
			    // ((struct pte*) (free_vpn * PAGESIZE))[stack_counter].valid = 1;
			    ((struct pte*) (free_vpn * PAGESIZE))[stack_counter].uprot = pt0[stack_counter].uprot;
			    ((struct pte*) (free_vpn * PAGESIZE))[stack_counter].kprot = pt0[stack_counter].kprot;
			    ((struct pte*) (free_vpn * PAGESIZE))[stack_counter].valid = pt0[stack_counter].valid;

				WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_0);

				//struct pte* addr = ((free_vpn<<PAGESHIFT)+(stack_counter)*(sizeof(struct pte)));
				//TracePrintf(9, "OLD VPN 508 has pfn %d\n", addr->pfn);
				// zero out this pt0 entry in the current process to be consistent
				((struct pte*) pt0)[free_vpn].valid = 0;
				//((struct pte*) pt0)[free_vpn].pfn = ;
				WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_0);
				// ((struct pte*) pt0)[free_vpn].pfn = 250;


				
			}
			
		}
		TracePrintf(9, "Fork case done\n");
		// add the child process to parent
        pcb *child_iter = current_pcb -> child;
        if (child_iter == NULL) current_pcb -> child = new_processPCB;
        else {
        	while (child_iter -> next_child != NULL) child_iter = child_iter -> next_child;
        	child_iter -> next_child = new_processPCB;
        }

		//Sibling Q stuff
		linkedList_add(2,new_processPCB);

		// enqueue two process to ready Queue
		//TracePrintf(0, "new_processPCB PID %d\n", new_processPCB->pid);
		//TracePrintf(0, "current_pcb PID %d\n", current_pcb->pid);
		linkedList_add(1,new_processPCB);
		linkedList_add(1,current_pcb);

		//ContextSwitch to enable new process

		ContextSwitch(MySwitchFunc, &current_pcb->ctx, (void *)current_pcb, (void *)linkedList_remove(1));

		TracePrintf(0, "Returning from Fork as parent, new process PID is %d\n", new_processPCB->pid);

		TracePrintf(0, "<Fork> DONE for pid: %d\n", current_pcb->pid);
		TracePrintf(0, "[Fork] DONE\n");
        return new_processPCB->pid;

	}
}

int ExecFunc(char *filename, char **argvec, ExceptionInfo *info) {
	//if (filename > )
	// filename 

	TracePrintf(0, "<Exec> \n", filename);
	int load_success;
	load_success = LoadProgram(filename, argvec, info);

	if (load_success != 0) {
		TracePrintf(0, "<Exec>: Load program failed\n");
		return ERROR;
	}
	TracePrintf(0, "[ExecFunc] DONE\n");
	return 0;
}

// void Exit(int number) {
// 	fprintf(stderr, "Exit Halt() %s\n");
// 	Halt();
// }

// 1. add next_sibling_proc 
// 2. next_sibling_proc related stuff
// 3. 
void Exit(int status) {
	TracePrintf(0, "    [EXIT] pid %d\n", current_pcb->pid);
	current_pcb -> status = TERMINATED;
	
	/* TERMINATE PROCESS */
	// if this process has children, make them orphan process
	if (current_pcb -> child != NULL) {
		struct pcb *child_pcb = current_pcb -> child;
		while (child_pcb != NULL) {
			child_pcb -> parent = NULL;
			child_pcb = child_pcb->next_child;
		}
	}

	// if this process has a parent
	if (current_pcb -> parent != NULL) {
		// add this child process to its parent's ExitChild queue
		ecb *exited_childq_block =  malloc(sizeof(ecb));
		exited_childq_block -> pid = current_pcb -> pid;
		exited_childq_block -> status = status;
		enqueue_ecb(current_pcb -> parent, exited_childq_block);

		// adjust parent's Children queue
		update_children_q(current_pcb -> parent, current_pcb);

		// case where if the parent was waiting, change status, and put to ready queue
		if ((current_pcb -> parent) -> status == WAITING) {
			(current_pcb -> parent) -> status = READY;
			linkedList_add(ReadyQueue, current_pcb -> parent);
		}
	}
	/* END */

	ContextSwitch(MySwitchFunc, &(current_pcb -> ctx), (void *) current_pcb, linkedList_remove(ReadyQueue));

	while(1) {} // Exit should never return
}

void enqueue_ecb(pcb *parent, ecb *exit_child) {
	if (parent -> exited_childq_head == NULL) {
		parent -> exited_childq_head = exit_child;
		parent -> exited_childq_tail = exit_child;
	} else {
		(parent -> exited_childq_tail) -> next = exit_child;
		parent -> exited_childq_tail = exit_child;
	}
}

void update_children_q(pcb *parent, pcb *exit_child) {
	pcb *child = parent -> child;
	if (child -> pid == exit_child -> pid) {
		parent -> child = (parent -> child) -> next_child;
		return;
	}
	while ((child -> next_child) -> pid != exit_child -> pid) {
		child = child -> next_child;
	}
	child -> next_child = exit_child -> next_child;
}

int Wait(int *status_ptr) {
	TracePrintf(0, "[WAIT] current process has pid: %d\n", current_pcb->pid);
	if (current_pcb -> exited_childq_head == NULL) {
		if (current_pcb -> child == NULL) {
			fprintf(stderr, "   [WAIT_ERROR]: no more children of current process.\n");
			return ERROR;
		}
		current_pcb -> status = WAITING;
		ContextSwitch(MySwitchFunc, &current_pcb -> ctx, (void *) current_pcb, linkedList_remove(ReadyQueue));
	}
	TracePrintf(0, "[WAIT] dubugging. Exited child head: %d\n", current_pcb -> exited_childq_head == NULL);

	// write to the status pointer 
	*status_ptr = (current_pcb -> exited_childq_head -> status);

	// kick this newly exited child out of parent's queue
	ecb *temp = current_pcb -> exited_childq_head;
	int return_id = temp -> pid;
	current_pcb -> exited_childq_head = temp -> next;
	if (current_pcb -> exited_childq_head == NULL) {
		current_pcb -> exited_childq_tail = NULL;
	}
	free(temp);
	TracePrintf(0, "[WAIT] returned child pid %d\n", return_id);


	// return process ID of the head exited child
	return return_id;

}


/*
 *  Load a program into the current process's address space.  The
 *  program comes from the Unix file identified by "name", and its
 *  arguments come from the array at "args", which is in standard
 *  argv format.
 *
 *  Returns:
 *      0 on success
 *     -1 on any error for which the current process is still runnable
 *     -2 on any error for which the current process is no longer runnable
 *
 *  This function, after a series of initial checks, deletes the
 *  contents of Region 0, thus making the current process no longer
 *  runnable.  Before this point, it is possible to return ERROR
 *  to an Exec() call that has called LoadProgram, and this function
 *  returns -1 for errors up to this point.  After this point, the
 *  contents of Region 0 no longer exist, so the calling user process
 *  is no longer runnable, and this function returns -2 for errors
 *  in this case.
 */
int
LoadProgram(char *name, char **args, ExceptionInfo *info)
{
    int fd;
    int status;
    struct loadinfo li;
    char *cp;
    char *cp2;
    char **cpp;
    char *argbuf;
    int i;
    unsigned long argcount;
    int size;
    int text_npg;
    int data_bss_npg;
    int stack_npg;
    //struct pte *pt0 = NULL;

    //struct pte *load_pt0 = (struct pte *) (VMEM_1_LIMIT - 2 * PAGESIZE);

    TracePrintf(0, "LoadProgram '%s', args %p\n", name, args);

    if ((fd = open(name, O_RDONLY)) < 0) {
	TracePrintf(0, "LoadProgram: can't open file '%s'\n", name);
	return (-1);
    }

    status = LoadInfo(fd, &li);
    TracePrintf(0, "LoadProgram: LoadInfo status %d\n", status);
    switch (status) {
	case LI_SUCCESS:
	    break;
	case LI_FORMAT_ERROR:
	    TracePrintf(0,
		"LoadProgram: '%s' not in Yalnix format\n", name);
	    close(fd);
	    return (-1);
	case LI_OTHER_ERROR:
	    TracePrintf(0, "LoadProgram: '%s' other error\n", name);
	    close(fd);
	    return (-1);
	default:
	    TracePrintf(0, "LoadProgram: '%s' unknown error\n", name);
	    close(fd);
	    return (-1);
    }
    TracePrintf(0, "text_size 0x%lx, data_size 0x%lx, bss_size 0x%lx\n",
	li.text_size, li.data_size, li.bss_size);
    TracePrintf(0, "entry 0x%lx\n", li.entry);

    /*
     *  Figure out how many bytes are needed to hold the arguments on
     *  the new stack that we are building.  Also count the number of
     *  arguments, to become the argc that the new "main" gets called with.
     */
    size = 0;
    for (i = 0; args[i] != NULL; i++) {
	size += strlen(args[i]) + 1;
    }
    argcount = i;
    TracePrintf(0, "LoadProgram: size %d, argcount %d\n", size, argcount);

    /*
     *  Now save the arguments in a separate buffer in Region 1, since
     *  we are about to delete all of Region 0.
     */
    cp = argbuf = (char *)malloc(size);
    for (i = 0; args[i] != NULL; i++) {
	strcpy(cp, args[i]);
	cp += strlen(cp) + 1;
    }
  
    /*
     *  The arguments will get copied starting at "cp" as set below,
     *  and the argv pointers to the arguments (and the argc value)
     *  will get built starting at "cpp" as set below.  The value for
     *  "cpp" is computed by subtracting off space for the number of
     *  arguments plus 4 (for the argc value, a 0 (AT_NULL) to
     *  terminate the auxiliary vector, a NULL pointer terminating
     *  the argv pointers, and a NULL pointer terminating the envp
     *  pointers) times the size of each (sizeof(void *)).  The
     *  value must also be aligned down to a multiple of 8 boundary.
     */
    cp = ((char *)USER_STACK_LIMIT) - size;
    cpp = (char **)((unsigned long)cp & (-1 << 4));	/* align cpp */
    cpp = (char **)((unsigned long)cpp - ((argcount + 4) * sizeof(void *)));

    text_npg = li.text_size >> PAGESHIFT;
    data_bss_npg = UP_TO_PAGE(li.data_size + li.bss_size) >> PAGESHIFT;
    stack_npg = (USER_STACK_LIMIT - DOWN_TO_PAGE(cpp)) >> PAGESHIFT;

    TracePrintf(0, "LoadProgram: text_npg %d, data_bss_npg %d, stack_npg %d\n",
	text_npg, data_bss_npg, stack_npg);

    /*
     *  Make sure we will leave at least one page between heap and stack
     */
    if (MEM_INVALID_PAGES + text_npg + data_bss_npg + stack_npg +
	1 + KERNEL_STACK_PAGES >= PAGE_TABLE_LEN) {
	TracePrintf(0, "LoadProgram: program '%s' size too large for VM\n",
	    name);
	free(argbuf);
	close(fd);
	return (-1);
    }

    /*
     *  And make sure there will be enough physical memory to
     *  load the new program.
     */

    // >>>> The new program will require text_npg pages of text,
    // >>>> data_bss_npg pages of data/bss, and stack_npg pages of
    // >>>> stack.  In checking that there is enough free physical
    // >>>> memory for this, be sure to allow for the physical memory
    // >>>> pages already allocated to this process that will be
    // >>>> freed below before we allocate the needed pages for
    // >>>> the new program being loaded.
    size_t pages_needed = text_npg + data_bss_npg + stack_npg;

    // >>>> not enough free physical memory
    if (pages_needed > free_pages_counter) {
	TracePrintf(0,
	    "LoadProgram: program '%s' size too large for physical memory\n",
	    name);
	free(argbuf);
	close(fd);
	return (-1);
    }

    // >>>> Initialize sp for the current process to (char *)cpp.
    // >>>> The value of cpp was initialized above.

    info->sp = (char *)cpp;

    /*
     *  Free all the old physical memory belonging to this process,
     *  but be sure to leave the kernel stack for this process (which
     *  is also in Region 0) alone.
     */
    // >>>> Loop over all PTEs for the current process's Region 0,
    // >>>> except for those corresponding to the kernel stack (between
    // >>>> address KERNEL_STACK_BASE and KERNEL_STACK_LIMIT).  For
    // >>>> any of these PTEs that are valid, free the physical memory
    // >>>> memory page indicated by that PTE's pfn field.  Set all
    // >>>> of these PTEs to be no longer valid.

    // should I loop over KERNEL_STACK_LIMIT - KERNEL_STACK_BASE?
    TracePrintf(9, "LoadProgram part 0 done\n");
    int pt_iter_load;
    for (pt_iter_load = 0; pt_iter_load < (PAGE_TABLE_LEN - KERNEL_STACK_PAGES); pt_iter_load++) {
    	// TracePrintf(9, "LoadProgram part 1 vpn:%d\n", pt_iter_load);
    	// TracePrintf(9, "The vpn of the pte is %d\n", (&page_table_region_0[pt_iter_load]-page_table_region_0)/sizeof(struct pte));        
    	//TracePrintf(10, "The vpn of the pte is %d\n", (&pt0[pt_iter_load] - pt0)/sizeof(struct pte));
        //int pt_index = PAGE_TABLE_LEN - pt_iter0 - 1;
        if (pt0[pt_iter_load].valid == 1) {
        	TracePrintf(9, "LoadProgram part 1 hello vpn:%d\n", pt_iter_load);
            // free page list
            deallocate_new_pfn(pt0[pt_iter_load].pfn);
            // free_physical_pages[free_pages_counter++] = page_table_region_0[pt_iter_load].pfn;

            // set valid bit to 0
            pt0[pt_iter_load].valid = 0;
        }
    }

    TracePrintf(9, "LoadProgram part 1 done\n");



    /*
     *  Fill in the page table with the right number of text,
     *  data+bss, and stack pages.  We set all the text pages
     *  here to be read/write, just like the data+bss and
     *  stack pages, so that we can read the text into them
     *  from the file.  We then change them read/execute.
     */

    // >>>> Leave the first MEM_INVALID_PAGES number of PTEs in the
    // >>>> Region 0 page table unused (and thus invalid)

    //????????????????

    int pt_iter_unused;
    for (pt_iter_load = 0; pt_iter_load < MEM_INVALID_PAGES; pt_iter_load++) {
        //int pt_index = PAGE_TABLE_LEN - pt_iter0 - 1;
        pt0[pt_iter_load].valid = 0;

        // take out free phys pages
        // free_physical_pages[free_pages_counter++] = pt0[pt_iter_load].pfn;

    }

    /* First, the text pages */
    // >>>> For the next text_npg number of PTEs in the Region 0
    // >>>> page table, initialize each PTE:
    // >>>>     valid = 1
    // >>>>     kprot = PROT_READ | PROT_WRITE
    // >>>>     uprot = PROT_READ | PROT_EXEC
    // >>>>     pfn   = a new page of physical memory
    
    int pt_iter_text;
    for (pt_iter_text = MEM_INVALID_PAGES; pt_iter_text < MEM_INVALID_PAGES + text_npg; pt_iter_text++) {
        // page_table_region_0[pt_iter_text].pfn = free_physical_pages[-- free_pages_counter];
        pt0[pt_iter_text].pfn = allocate_new_pfn();
        pt0[pt_iter_text].uprot = (PROT_READ | PROT_EXEC);
        pt0[pt_iter_text].kprot = (PROT_READ | PROT_WRITE);
        pt0[pt_iter_text].valid = 1;
    }
    TracePrintf(9, "LoadProgram part 2 done\n");

    /* Then the data and bss pages */
    // >>>> For the next data_bss_npg number of PTEs in the Region 0
    // >>>> page table, initialize each PTE:
    // >>>>     valid = 1
    // >>>>     kprot = PROT_READ | PROT_WRITE
    // >>>>     uprot = PROT_READ | PROT_WRITE
    // >>>>     pfn   = a new page of physical memory

    int pt_iter_databss;
    for (pt_iter_databss = (MEM_INVALID_PAGES + text_npg); pt_iter_databss < MEM_INVALID_PAGES + 
        text_npg + data_bss_npg; pt_iter_databss++) {
        //page_table_region_0[pt_iter_databss].pfn = free_physical_pages[--free_pages_counter];
    	pt0[pt_iter_databss].pfn = allocate_new_pfn();
        pt0[pt_iter_databss].uprot = (PROT_READ | PROT_WRITE);
        pt0[pt_iter_databss].kprot = (PROT_READ | PROT_WRITE);
        pt0[pt_iter_databss].valid = 1;
    }    

    current_pcb->brk_pos = MEM_INVALID_PAGES + text_npg + data_bss_npg;
    TracePrintf(9, "LoadProgram part 3 done\n");

    /* And finally the user stack pages */
    // >>>> For stack_npg number of PTEs in the Region 0 page table
    // >>>> corresponding to the user stack (the last page of the
    // >>>> user stack *ends* at virtual address USER_STACK_LIMIT),
    // >>>> initialize each PTE:
    // >>>>     valid = 1
    // >>>>     kprot = PROT_READ | PROT_WRITE
    // >>>>     uprot = PROT_READ | PROT_WRITE
    // >>>>     pfn   = a new page of physical memory

    int pt_iter_stack;
    int u_stack = (USER_STACK_LIMIT >> PAGESHIFT);
    for (pt_iter_stack = u_stack - stack_npg; pt_iter_stack < u_stack; pt_iter_stack++) {
        // page_table_region_0[pt_iter_stack].pfn = free_physical_pages[--free_pages_counter];
        pt0[pt_iter_stack].pfn = allocate_new_pfn();
        pt0[pt_iter_stack].uprot = (PROT_READ | PROT_WRITE);
        pt0[pt_iter_stack].kprot = (PROT_READ | PROT_WRITE);
        pt0[pt_iter_stack].valid = 1;
    }     

    current_pcb->user_stack_low_vpn = u_stack-stack_npg; 


    /*
     *  All pages for the new address space are now in place.  Flush
     *  the TLB to get rid of all the old PTEs from this process, so
     *  we'll be able to do the read() into the new pages below.
     */
    WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_0);

    /*
     *  Read the text and data from the file into memory.
     */
    if (read(fd, (void *)MEM_INVALID_SIZE, li.text_size+li.data_size)
	!= li.text_size+li.data_size) {
	TracePrintf(0, "LoadProgram: couldn't read for '%s'\n", name);
	free(argbuf);
	close(fd);
	// >>>> Since we are returning -2 here, this should mean to
	// >>>> the rest of the kernel that the current process should
	// >>>> be terminated with an exit status of ERROR reported
	// >>>> to its parent process.
	return (-2);
    }

    close(fd);			/* we've read it all now */

    /*
     *  Now set the page table entries for the program text to be readable
     *  and executable, but not writable.
     */
    // >>>> For text_npg number of PTEs corresponding to the user text
    // >>>> pages, set each PTE's kprot to PROT_READ | PROT_EXEC.'

    int pt_iter_text_1;
    for (pt_iter_text_1 = MEM_INVALID_PAGES; pt_iter_text < MEM_INVALID_PAGES + text_npg; pt_iter_text++) {
        pt0[pt_iter_text].kprot = (PROT_READ | PROT_EXEC);
    }   

    WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_0);

    /*
     *  Zero out the bss
     */
    memset((void *)(MEM_INVALID_SIZE + li.text_size + li.data_size),
	'\0', li.bss_size);

    /*
     *  Set the entry point in the exception frame.
     */

    //>>>> Initialize pc for the current process to (void *)li.entry
    info -> pc = (void *)li.entry;

    /*
     *  Now, finally, build the argument list on the new stack.
     */
    *cpp++ = (char *)argcount;		/* the first value at cpp is argc */
    cp2 = argbuf;
    for (i = 0; i < argcount; i++) {      /* copy each argument and set argv */
	*cpp++ = cp;
	strcpy(cp, cp2);
	cp += strlen(cp) + 1;
	cp2 += strlen(cp2) + 1;
    }
    free(argbuf);
    *cpp++ = NULL;	/* the last argv is a NULL pointer */
    *cpp++ = NULL;	/* a NULL pointer for an empty envp */
    *cpp++ = 0;		/* and terminate the auxiliary vector */

    /*
     *  Initialize all regs[] registers for the current process to 0,
     *  initialize the PSR for the current process also to 0.  This
     *  value for the PSR will make the process run in user mode,
     *  since this PSR value of 0 does not have the PSR_MODE bit set.
     */
    // >>>> Initialize regs[0] through regs[NUM_REGS-1] for the
    // >>>> current process to 0.
    // >>>> Initialize psr for the current process to 0.
    // not sure
    for (i = 0; i <= NUM_REGS-1; i++) {
        info -> regs[i] = 0;
    }
    info -> psr = 0;
    
    //PSR_MODE = 0;
    TracePrintf(0, "[LoadProgram] DONE\n");
    return (0);
}


/* --------------------------TtyWrite Stuff------------------------------ */


pcb *whichWrite_Head(int term) {
	pcb *Write_BlockQ;
    	if (term == 0) {
    		Write_BlockQ = Write_BlockQ_0_head;
    	} else if (term == 1) {
    		Write_BlockQ = Write_BlockQ_1_head;
    	} else if (term == 2) {
    		Write_BlockQ = Write_BlockQ_2_head;
    	} else if (term == 3) {
    		Write_BlockQ = Write_BlockQ_3_head;
    	}
    return Write_BlockQ;
}

void trap_tty_transmit(ExceptionInfo *info) {
	TracePrintf(0, "[trap_tty_transmit], starts, pid %d\n", current_pcb->pid);
	int term_num = info->code;
	pcb *block_head = whichWrite_Head(term_num);
	terminal[term_num]->term_writing = 0;
	// add blcok queue head to readyQueue
	if(block_head != NULL) {
		TracePrintf(0, "trap_tty_transmit, add blcok queue head to readyQueue, blcok queue pid is %d\n", block_head->pid);
		linkedList_add(ReadyQueue,linkedList_remove(term_num+8));
	}
	TracePrintf(0, "[trap_tty_transmit] DONE \n");
};

int TtyWrite(int tty_id, void *buf, int len) {
	TracePrintf(0, "[TtyWrite] Starts \n");
	pcb *block_head = whichWrite_Head(tty_id);
	if (len < 0) {
		return ERROR;
	}
	if (len == 0) {
		return 0;
	}
	TracePrintf(0, "[TtyWrite] HERE1 \n");
	// if there the terminal is writing, add current pcb to block queue
	TracePrintf(0, "terminal[tty_id]->term_writing is  %d\n", terminal[tty_id]->term_writing);
    if (terminal[tty_id]->term_writing == 1) {
    	TracePrintf(0, "if there the terminal is writing \n");
    	// add to block queue
    	linkedList_add(tty_id+8, current_pcb);
    	if (ready_q_head != NULL) {
    		TracePrintf(0, "[TtyWrite] ContextSwitch1 \n");
    		ContextSwitch(MySwitchFunc, &(current_pcb -> ctx), (void *) current_pcb, linkedList_remove(ReadyQueue));
    	} else {
    		TracePrintf(0, "[TtyWrite] ContextSwitch2 \n");
    		ContextSwitch(MySwitchFunc, &(current_pcb -> ctx), (void *) current_pcb, idle_pcb);
    	}
    	
    }
    TracePrintf(0, "[TtyWrite] HERE2 \n");
    // otherwise ttyTransmit
    terminal[tty_id]->term_writing = 1;
    char *bufTemp = (char*) malloc(TERMINAL_MAX_LINE * sizeof(char));
    memcpy(bufTemp, buf, len);
    TracePrintf(0, "[TtyWrite] Before TtyTransmit \n");
    TtyTransmit(tty_id, bufTemp, len);
    TracePrintf(0, "[TtyWrite] After TtyTransmit \n");
	// if (ready_q_head != NULL) {
	// 	TracePrintf(0, "[TtyWrite] ContextSwitch3 \n");
	// 	ContextSwitch(MySwitchFunc, &(current_pcb -> ctx), (void *) current_pcb, linkedList_remove(ReadyQueue));
	// } else {
	// 	TracePrintf(0, "[TtyWrite] ContextSwitch4 \n");
	// 	if (ready_q_head == NULL) {
	// 		TracePrintf(0, "[TtyWrite] ready_q_head IS NULLLLLLL \n");
	// 	}
		
	// 	ContextSwitch(MySwitchFunc, &(current_pcb -> ctx), (void *) current_pcb, idle_pcb);
	// }
	TracePrintf(0, "[TtyWrite] DONE \n");    
    //ContextSwitch(MySwitchFunc, &(current_pcb -> ctx), (void *) current_pcb, linkedList_remove(ReadyQueue));
    return len;
}
