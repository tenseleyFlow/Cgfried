#define cat(a,b) a##b
cat(x,y) cat(x,) cat(,y) cat(,)
#define cat3(a,b,c) a##b##c
cat3(1,,3) cat3(,,) cat3(x,y,z)
