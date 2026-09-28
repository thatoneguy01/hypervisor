li $1,2147483647
li $2,-1

add $3,$1,$2
sub $4,$2,$1

li $5,-2147483648
addi $6,$5,1

mul $7,$2,$5

addu $8,$1,$2
subu $9,$5,$1

DUMP_PROCESSOR_STATE
