BEGIN {
    # Closed audited backend baseline. The spellings are GNU objdump output,
    # not assembler source aliases. Unknown instructions fail by default.
    split("add addb addw addl addq and andb andw andl andq call cltd cmp cmpb cmpw cmpl cmpq cqto div divb divw divl divq idiv idivb idivw idivl idivq imul imulb imulw imull imulq ja jae jb jbe je jg jge jl jle jmp jne jnp jp lea leal leaq mov movb movw movl movq movabs movsbl movsbq movsbw movslq movswl movswq movzbl movzbq movzbw movzwl movzwq neg negb negw negl negq nop nopl nopw not notb notw notl notq or orb orw orl orq pop push ret sar sarb sarw sarl sarq seta setae setb setbe sete setg setge setl setle setne setnp setp shl shlb shlw shll shlq shr shrb shrw shrl shrq sub subb subw subl subq test testb testw testl testq ud2 xchg xor xorb xorw xorl xorq", a)
    for (i in a) baseline[a[i]] = 1

    # Scalar and packed SSE/SSE2 emitted by the backend, plus PEXTRW's
    # original register-destination form pinned below.
    split("addpd addps addsd addss andpd andps cvtsd2ss cvtsi2sd cvtsi2ss cvtss2sd cvttsd2si cvttss2si divpd divps divsd divss movd movdqu movq movsd movss mulpd mulps mulsd mulss paddb paddd paddq paddw pand pextrw pmullw por pshufd pshuflw psrldq psubb psubd psubq psubw punpcklbw punpcklqdq punpcklwd pxor subpd subps subsd subss ucomisd ucomiss xorpd xorps verr verw", a)
    for (i in a) baseline[a[i]] = 1

    # Exhaustive GNU objdump spellings for the Sprint 23 x87 backend.
    split("fld flds fldl fldt fld1 fldz fstps fstpl fstpt fildl fildll fistpl fistpll faddp fsubp fsubrp fmulp fdivp fdivrp fchs fabs fucomip fstp fnstcw fldcw", a)
    for (i in a) x87_ok[a[i]] = 1
}

function reject(why, mnemonic, record) {
    printf "check_isa: %s: %s in %s: %s\n", why, mnemonic, object, record > "/dev/stderr"
    failures++
}

# With --no-show-raw-insn, a decoded record begins with a hexadecimal address
# and colon. Symbol labels have text between the address and colon and cannot
# match this shape.
/^[[:space:]]*[[:xdigit:]]+:[[:space:]]/ {
    record = $0
    sub(/^[[:space:]]*[[:xdigit:]]+:[[:space:]]*/, "", record)
    count = split(record, field, /[[:space:]]+/)
    field_index = 1
    while (field_index <= count) {
        candidate = tolower(field[field_index])
        # These are semantic extension prefixes, not harmless legacy decode
        # decoration. Deny them even when the following mnemonic is baseline.
        if (candidate == "bnd" || candidate == "notrack" ||
            candidate == "xacquire" || candidate == "xrelease") {
            reject("post-SSE2 instruction prefix exceeds SSE2 ceiling", candidate, record)
            next
        }
        if (candidate == "addr16" || candidate == "addr32" ||
            candidate == "cs" ||
            candidate == "data16" || candidate == "ds" ||
            candidate == "es" || candidate == "fs" ||
            candidate == "gs" || candidate == "lock" ||
            candidate == "rep" ||
            candidate == "repe" || candidate == "repne" ||
            candidate == "repnz" || candidate == "repz" ||
            candidate == "ss" ||
            candidate ~ /^rex([.][wrxb]+)?$/) {
            field_index++
            continue
        }
        break
    }
    if (field_index > count)
        next
    mnemonic = tolower(field[field_index])

    # GNU objdump uses AT&T operand order. The original SSE2 PEXTRW writes a
    # GP register; SSE4.1 added the memory-destination form.
    if (mnemonic == "pextrw") {
        operands = record
        sub(/^([^[:space:]]+[[:space:]]+)*/, "", operands)
        destination = operands
        sub(/^.*,/, "", destination)
        sub(/^[[:space:]]*/, "", destination)
        if (destination !~ /^%/) {
            reject("SSE4.1 memory-destination instruction exceeds SSE2 ceiling", mnemonic, record)
            next
        }
    }

    if (mnemonic == "fisttp" || mnemonic == "fisttpl" ||
        mnemonic == "fisttpq" || mnemonic == "fisttps" ||
        mnemonic == "fisttpw" || mnemonic == "fisttpll") {
        reject("fisttp is forbidden even for long double", mnemonic, record)
        next
    }
    if (mnemonic ~ /^f/ && !(mnemonic in x87_ok)) {
        reject("x87 spelling is outside the Sprint 23 whitelist", mnemonic, record)
        next
    }
    if ((mnemonic in x87_ok) && !x87_licensed) {
        reject("x87 instruction lacks a source-text long double license", mnemonic, record)
        next
    }
    if (!(mnemonic in x87_ok) && !(mnemonic in baseline)) {
        reject("instruction is outside the closed x86-64/SSE2 baseline", mnemonic, record)
        next
    }
}

END { exit(failures != 0) }
