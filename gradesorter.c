#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>

#define TRUE 1
#define FALSE 0
#define SORT_BY_NAME (-1) /* sort_index sentinel meaning "sort by student name" (from -s 1 or -s Name) */
#define COLUMN_NOT_FOUND (-2) /* get_index() sentinel: distinct from SORT_BY_NAME so "-s Name" isn't mistaken for "not found" */

typedef struct {
    char **names;   /* header column names, names[0] is always "Name" */
    int    count;   /* total header columns, including "Name" */
} Header;

typedef struct {
    char *name;
    float *scores;
    int num_scores;  /* number of scores, excluding the name */
    float average;  /* average score, computed after reading all scores */
} Student;



/* Allocation tracking: every xmalloc/xrealloc/xfopen registers itself here,
 * and cleanup_all() (run automatically via atexit, registered in main())
 * frees/closes whatever is still registered. This guarantees every
 * allocation is released on exit -- including the exit(1) calls inside
 * xmalloc/xrealloc themselves -- without duplicating cleanup logic at every
 * call site. Always free/close through xfree()/xfclose() (never raw
 * free()/fclose()) so the registry never holds a stale entry. */
typedef enum { RES_MALLOC, RES_FILE } ResourceKind; //Res_malloc is for memory allocations, RES_FILE is for file pointers
typedef struct { void *ptr; ResourceKind kind; } Resource; //struct to hold the pointer and its kind (malloc or file)

static Resource *registry = NULL;
static int registry_count = 0;
static int registry_capacity = 0;

static void track(void *ptr, ResourceKind kind) {
    if (registry_count == registry_capacity) {
        registry_capacity = registry_capacity ? registry_capacity * 2 : 16; //double capacity if it exists, else start with 16
        // plain realloc here, not xrealloc -- the tracker must not track itself
        Resource *grown = realloc(registry, (size_t)registry_capacity * sizeof(*registry));
        if (!grown) {
            fprintf(stderr, "gradesorter: out of memory (tracking allocations)\n");
            exit(1);
        }
        registry = grown;
    }
    registry[registry_count].ptr = ptr;
    registry[registry_count].kind = kind;
    registry_count++;
}

static void untrack(void *ptr) {
    for (int i = 0; i < registry_count; i++) {
        if (registry[i].ptr == ptr) {
            registry[i] = registry[registry_count - 1];
            registry_count--;
            return;
        }
    }
}

static void cleanup_all(void) {
    for (int i = 0; i < registry_count; i++) {
        if (registry[i].kind == RES_MALLOC) {
            free(registry[i].ptr);
        } else {
            FILE *f = (FILE *)registry[i].ptr;
            if (f != stdin && f != stdout) {
                fclose(f);
            }
        }
    }
    free(registry);
    registry = NULL;
    registry_count = 0;
    registry_capacity = 0;
}

static void *xmalloc(size_t size) {
    void *p = malloc(size);
    if (!p) {
        fprintf(stderr, "gradesorter: out of memory (malloc %zu bytes)\n", size);
        exit(1);
    }
    track(p, RES_MALLOC);
    return p;
}

static void *xrealloc(void *ptr, size_t size) {
    void *p = realloc(ptr, size);
    if (!p) {
        fprintf(stderr, "gradesorter: out of memory (realloc %zu bytes)\n", size);
        exit(1);
    }
    if (p != ptr) {
        if (ptr) untrack(ptr);
        track(p, RES_MALLOC);
    }
    return p;
}

static void xfree(void *ptr) {
    if (ptr) {
        untrack(ptr);
        free(ptr);
    }
}

static FILE *xfopen(const char *path, const char *mode) {
    FILE *f = fopen(path, mode);
    if (f) {
        track(f, RES_FILE);
    }
    return f;
}

static void xfclose(FILE *f) {
    if (f && f != stdin && f != stdout) {
        untrack(f);
        fclose(f);
    }
}

Header header; //global variable to hold header information
int sort_index; //global variable to hold the index of the column to sort by

/* forward declarations so functions can be used before their definitions below */
static char **split_csv_line(const char *line, int *num_fields);
static Student *read_students(const char *input_file_name, Header *header, int *students_count);
static float getAverage(Student student, int num_scores);
int get_index(Header header, const char *column_name);
int lexicographical_comparator(const void *a, const void *b);
int ascending_comparator(const void *a, const void *b);
int descending_comparator(const void *a, const void *b);
void write_students(FILE *output_file, Header header, Student *students, int students_count, int col_index);

int main(int argc, char *argv[]) {

    atexit(cleanup_all); // safety net: frees/closes anything still tracked on any exit path

    int opt;
    int sort_by_index = FALSE;
    long numeric_column_arg = -1; /* 1-based column number if -s was given a number */
    char *sort_column_name = "Average";
    int descending_order = TRUE;
    int include_missing_info = FALSE;
    char *input_file_name = NULL;
    int output_file_specified = FALSE;
    char *output_file_name = NULL;
    while((opt = getopt(argc, argv, "i:o:s:rz")) != -1) {
        switch(opt) {
            case 'i':
                input_file_name = optarg; //fine since arguments will be valid and not freed until program exits
                break;
            case 'o':
                output_file_name = optarg; //fine since arguments will be valid and not freed until program exits
                output_file_specified = TRUE;
                break;
            case 's': {
                char *endptr;
                long sort_column_index = strtol(optarg, &endptr, 10);
                if (*endptr == '\0' && sort_column_index >= 1) {
                    // optarg is a valid 1-based index
                    numeric_column_arg = sort_column_index;
                    sort_by_index = TRUE;
                } else {
                    // optarg is a column name
                    sort_column_name = optarg; //fine since arguments will be valid and not freed until program exits
                    sort_by_index = FALSE;
                }
                break;
            }
            case 'r':
                descending_order = FALSE;
                break;
            case 'z':
                include_missing_info = TRUE;
                break;
            default:
                exit(1); //getopt already prints an error message -> just exit
        }
    }

    //get students and calculate their averages
    int students_count = 0;
    Student *students = read_students(input_file_name, &header, &students_count);
    for (int i = 0; i < students_count; i++) {
        students[i].average = getAverage(students[i], students[i].num_scores);
    }

    //Now we need to sort the students based on the specified column or index
    if (sort_by_index == FALSE) {
        sort_index = get_index(header, sort_column_name);
        if (sort_index == COLUMN_NOT_FOUND) {
            fprintf(stderr, "gradesorter: column name %s not found\n", sort_column_name);
            exit(1);
        }
    } else {
        // numeric_column_arg is the 1-based CSV column (column 1 = Name, column 2 = first score).
        // scores[] excludes the Name column, so convert to a 0-based scores[] index; column 1
        // lands on SORT_BY_NAME (-1) via this same formula (1 - 2 == -1).
        if (numeric_column_arg < 1 || numeric_column_arg > header.count) {
            fprintf(stderr, "gradesorter: column index %ld out of range\n", numeric_column_arg);
            exit(1);
        }
        sort_index = (int)numeric_column_arg - 2;
    }

    if (descending_order == TRUE) {
        qsort(students, students_count, sizeof(Student), descending_comparator);
    } else {
        qsort(students, students_count, sizeof(Student), ascending_comparator);
    }

    //Need to either include or exclude students with missing info based on the -z option
    if (include_missing_info == FALSE) {
        int valid_students_count = 0;
        for (int i = 0; i < students_count; i++) {
            int is_valid;
            if (sort_index == SORT_BY_NAME) {
                is_valid = (students[i].name != NULL); //missing name is the "missing value" when sorting by name
            } else {
                float value = (sort_index == header.count - 1)
                    ? students[i].average
                    : students[i].scores[sort_index];
                is_valid = !isnan(value); //only include students with a valid score in the target column
            }
            if (is_valid) {
                students[valid_students_count++] = students[i];
            } else {
                xfree(students[i].name);
                xfree(students[i].scores);
            }
        }
        students_count = valid_students_count;
    }

    //output students to specified output file or stdout
    FILE *output_file = stdout; //default to stdout
    if (output_file_specified == TRUE) {
        output_file = xfopen(output_file_name, "w");
        if (!output_file) {
            fprintf(stderr, "gradesorter: cannot open output file %s\n", output_file_name);
            exit(1); // nothing to clean up here manually -- cleanup_all() (via atexit) frees
                     // the still-tracked header/students automatically
        }
    }
    write_students(output_file, header, students, students_count, sort_index);

    // Free everything explicitly on the success path. cleanup_all() would also
    // catch anything left via atexit, but freeing here keeps the malloc->free
    // trace visible for a normal run and leaves the registry empty at exit.
    if (header.count > 0) {
        for (int i = 0; i < header.count; i++) {
            xfree(header.names[i]);
        }
        xfree(header.names);
    }
    for (int i = 0; i < students_count; i++) {
        xfree(students[i].name);
        xfree(students[i].scores);
    }
    xfree(students);
    xfclose(output_file);
    return 0;
}

void write_students(FILE *output_file, Header header, Student *students, int students_count, int col_index) {

    // Write student data
    for (int i = 0; i < students_count; i++) {
        // Sorting by name has no associated numeric column, so fall back to Average for the printed score.
        float value = (col_index == SORT_BY_NAME || col_index == header.count - 1)
            ? students[i].average
            : students[i].scores[col_index];
        const char *name = students[i].name ? students[i].name : "null"; //missing name prints as "null"
        fprintf(output_file, "%s score: %.2f\n", name, value);
    }
}

int ascending_comparator(const void *a, const void *b) {
    const Student *student_a = (const Student *)a;
    const Student *student_b = (const Student *)b;
    if (sort_index == SORT_BY_NAME) {
        // Ascending (A-Z) name order; lexicographical_comparator already sorts NULL names last.
        return lexicographical_comparator(a, b);
    }
    // Implement comparison logic based on the sort column
    float score_a = (sort_index == header.count - 1) ? student_a->average : student_a->scores[sort_index];
    float score_b = (sort_index == header.count - 1) ? student_b->average : student_b->scores[sort_index];
    // NaN (missing) values always sort to the bottom, regardless of direction
    if (isnan(score_a) && isnan(score_b)) return lexicographical_comparator(a, b);
    if (isnan(score_a)) return 1;
    if (isnan(score_b)) return -1;
    if (score_a < score_b) return -1;
    if (score_a > score_b) return 1;
    return lexicographical_comparator(a, b); // fallback to lexicographical comparison if scores are equal
}

int descending_comparator(const void *a, const void *b) {
    const Student *student_a = (const Student *)a;
    const Student *student_b = (const Student *)b;
    if (sort_index == SORT_BY_NAME) {
        // Descending (Z-A) name order, but a NULL name still sorts last, never first.
        if (!student_a->name && !student_b->name) return 0;
        if (!student_a->name) return 1;
        if (!student_b->name) return -1;
        return strcmp(student_b->name, student_a->name);
    }
    // Implement comparison logic based on the sort column
    float score_a = (sort_index == header.count - 1) ? student_a->average : student_a->scores[sort_index];
    float score_b = (sort_index == header.count - 1) ? student_b->average : student_b->scores[sort_index];
    // NaN (missing) values always sort to the bottom, regardless of direction
    if (isnan(score_a) && isnan(score_b)) return lexicographical_comparator(a, b);
    if (isnan(score_a)) return 1;
    if (isnan(score_b)) return -1;
    if (score_a > score_b) return -1;
    if (score_a < score_b) return 1;
    return lexicographical_comparator(a, b); // fallback to lexicographical comparison if scores are equal
}

int lexicographical_comparator(const void *a, const void *b) {
    const Student *student_a = (const Student *)a;
    const Student *student_b = (const Student *)b;
    // A NULL name (missing Name field) always sorts last, even as a tie-break.
    if (!student_a->name && !student_b->name) return 0;
    if (!student_a->name) return 1;
    if (!student_b->name) return -1;
    return strcmp(student_a->name, student_b->name);
}


int get_index(Header header, const char *column_name) {
    if (strcmp(column_name, header.names[0]) == 0) {
        return SORT_BY_NAME; // matches the first header column (always "Name")
    }
    // header.names[0] is "Name"; scores[] excludes it, so real columns map to i - 1
    for (int i = 1; i < header.count; i++) {
        if (strcmp(header.names[i], column_name) == 0) {
            return i - 1;
        }
    }
    if (strcmp(column_name, "Average") == 0) {
        return header.count - 1; // sentinel: one past the last real scores[] index
    }
    return COLUMN_NOT_FOUND;
}


static float getAverage(Student student, int num_scores) {
    float sum = 0;
    int count = 0;
    for (int i = 0; i < num_scores; i++) {
        if (!isnan(student.scores[i])) { //only include valid scores
            sum += student.scores[i];
            count++;
        }
    }
    return count > 0 ? sum / count : NAN; //return NaN if no valid scores
}



static Student *read_students(const char *input_file_name, Header *header, int *students_count) {
    FILE *input_file = stdin; //default to stdin
    if (input_file_name != NULL) {
        input_file = xfopen(input_file_name, "r");
        if (!input_file) {
            fprintf(stderr, "gradesorter: cannot open input file %s\n", input_file_name);
            exit(1);
        }
    }
    char *line = NULL;
    size_t line_size = 0;
    ssize_t read; //signed size_t -> -1 for error
    int first_line = TRUE;
    Student *students = NULL; //array of students
    while ((read = getline(&line, &line_size, input_file)) != -1) {
        if (read > 0 && line[read - 1] == '\n') {
            line[read - 1] = '\0';
        }
        int num_fields;
        char **fields = split_csv_line(line, &num_fields);
        if (first_line) {
            // Process header line
            *header = (Header){fields, num_fields};
            first_line = FALSE;
            // Do something with the header fields...
        } else {
            // Process student data line
            if (num_fields != header->count) {
                fprintf(stderr, "gradesorter: malformed CSV: row has %d fields, expected %d\n",
                        num_fields, header->count);
                xfree(line); // getline's buffer isn't reachable through the tracker; free explicitly
                exit(1);
            }
            Student student;
            student.name = fields[0]; // fields[0] ownership transfers to student.name
            student.num_scores = num_fields - 1;
            student.scores = xmalloc((size_t)(num_fields - 1) * sizeof(float));
            for (int i = 1; i < num_fields; i++) {
                if (fields[i] == NULL || strlen(fields[i]) == 0) {
                    student.scores[i - 1] = NAN; // Use NaN to indicate missing score
                } else {
                    student.scores[i - 1] = strtof(fields[i], NULL); // Convert score strings to floats
                    xfree(fields[i]);
                }
            }
            xfree(fields); // free the pointer array itself (fields[0] is kept via student.name)
            students = xrealloc(students, (size_t)(*students_count + 1) * sizeof(Student));
            students[*students_count] = student;
            (*students_count)++;
        }
    }
    xfree(line); // getline's buffer isn't reachable through the tracker; free explicitly
    xfclose(input_file); // no-op if input_file is stdin
    return students;
}



//splits each line into fields, returning an array of strings for each line
static char **split_csv_line(const char *line, int *num_fields) {
    char **fields = NULL; //character fields we find
    int count = 0;
    const char *field_start = line; //start of field to read
    const char *p = line; //pointer to iterate over field

    for(;;) { //double semicolon = infinite loop
        if (*p == ',' || *p == '\0') { //currently on comma or null value ('\0')
            size_t field_length = (size_t)(p - field_start); //length of field -> size_t big enough to hold largest array in C
            if (field_length == 0) { //if field is empty, set to NULL
                fields = xrealloc(fields, (count + 1) * sizeof(*fields)); //reallocate memory for fields array
                fields[count++] = NULL; //add NULL to fields array
            } else {
                fields = xrealloc(fields, (count + 1) * sizeof(*fields)); //reallocate memory for fields array
                char *field = xmalloc(field_length + 1); //allocate memory for field string
                memcpy(field, field_start, field_length); //copy field from line to field string (dest, src, length)
                field[field_length] = '\0'; //null terminate the field string
                fields[count++] = field; //add field to fields array
            }
            if (*p == '\0') { //if we are at the end of the line, break out of loop
                break;
            }
            field_start = p + 1; //move to next field (applies whether the field was empty or not)
        }
        p++; //move to next character
    }
    *num_fields = count; //set number of fields
    return fields; //return array of fields
}
