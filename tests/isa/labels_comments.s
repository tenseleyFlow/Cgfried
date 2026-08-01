.text
# vaddps and haddps in comments are not decoded instructions.
.globl isa_labels_comments
.type isa_labels_comments,@function
isa_labels_comments:
	ret
.size isa_labels_comments, .-isa_labels_comments

# Export mnemonic-looking function labels so GNU objdump prints them. They
# remain symbol records, never decoded instruction records.
.globl haddps
.type haddps,@function
haddps:
	ret
.size haddps, .-haddps

.globl vaddps
.type vaddps,@function
vaddps:
	ret
.size vaddps, .-vaddps
