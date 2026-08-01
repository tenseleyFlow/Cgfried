.text
.globl isa_monitor_mwait
.type isa_monitor_mwait,@function
isa_monitor_mwait:
	monitor
	mwait
	ret
.size isa_monitor_mwait, .-isa_monitor_mwait

