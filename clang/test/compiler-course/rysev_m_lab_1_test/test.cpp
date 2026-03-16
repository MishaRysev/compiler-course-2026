// RUN: %clang_cc1 -load %llvmshlibdir/rysev_m_lab_1_ClangAST%pluginext -plugin rysev_m_lab_1 -fsyntax-only %s 2>&1 | FileCheck --implicit-check-not="warning: resource leak" %s

extern "C" {
void* malloc(unsigned long);
void free(void*);
void* calloc(unsigned long, unsigned long);
void* realloc(void*, unsigned long);
typedef struct FILE FILE;
FILE* fopen(const char*, const char*);
int fclose(FILE*);
}

void test_new_leak() {
    int *p = new int;
    // CHECK: warning: resource leak: memory allocated here
}

void test_new_delete_ok() {
    int *p = new int;
    delete p;
}

void test_malloc_leak() {
    int *p = (int*)malloc(10 * sizeof(int));
    // CHECK: warning: resource leak: memory allocated here
}

void test_malloc_free_ok() {
    int *p = (int*)malloc(10 * sizeof(int));
    free(p);
}

void test_fopen_leak() {
    FILE *f = fopen("file.txt", "r");
    // CHECK: warning: resource leak: file allocated here
}

void test_fopen_fclose_ok() {
    FILE *f = fopen("file.txt", "r");
    fclose(f);
}

void test_assign_overwrite() {
    int *p = new int;
    // CHECK: warning: resource leak: memory allocated here
    p = new int;
    delete p;
}

void free_it(int *q) { delete q; }

void test_ptr_passed_to_func() {
    int *p = new int;
    // CHECK: warning: resource leak: memory allocated here
    free_it(p);
}

int* test_return_leak() {
    int *p = new int;
    // CHECK: warning: resource leak: memory allocated here
    return p;
}

void test_multiple_leaks() {
    int *a = new int;
    // CHECK: warning: resource leak: memory allocated here
    int *b = new int;
    // CHECK: warning: resource leak: memory allocated here
    delete a;
}

void test_calloc_leak() {
    int *p = (int*)calloc(10, sizeof(int));
    // CHECK: warning: resource leak: memory allocated here
}

void test_realloc_leak() {
    int *p = (int*)realloc(0, 10 * sizeof(int));
    // CHECK: warning: resource leak: memory allocated here
}

void test_free_direct() {
    free(malloc(10));
}

void test_no_warning_ok() {
    int *p = new int;
    delete p;
    FILE *f = fopen("file.txt", "r");
    fclose(f);
}

void test_assign_null() {
    int *p = new int;
    // CHECK: warning: resource leak: memory allocated here
    p = 0;
}