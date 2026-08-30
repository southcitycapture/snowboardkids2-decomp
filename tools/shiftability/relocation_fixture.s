.set noreorder

.section .text
.globl shiftabilityFixtureLoad
shiftabilityFixtureLoad:
    lui     $v0, %hi(shiftabilityFixtureData)
    addiu   $v0, $v0, %lo(shiftabilityFixtureData)
    jr      $ra
     nop

.section .rodata
.globl shiftabilityFixturePointer
shiftabilityFixturePointer:
    .word shiftabilityFixtureData

.section .data
.globl shiftabilityFixtureData
shiftabilityFixtureData:
    .word 0x53484946
