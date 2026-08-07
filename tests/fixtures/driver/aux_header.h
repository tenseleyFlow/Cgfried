/* A header handed to the driver ON THE COMMAND LINE, which gcc dispatches
 * to the compiler as a header to precompile and never to the linker.
 * tests/programs/driver/header_input_not_linked.c is the consumer. */
#ifndef AUX_HEADER_H
#define AUX_HEADER_H

#define AUX_HEADER_ANSWER 42

int aux_header_answer(void);

#endif
