# Assignment: UW Grade Sorter (`gradesorter`)

## Learning Objectives
- Re-familiarize yourself with the **C programming language**.
- Practice using `getopt()` for command-line argument parsing.
- Master Unix input/output redirection (handling `stdin` and `stdout`).
- Practice file I/O, dynamic memory allocation (`malloc`, `realloc`, `free`), string processing, and custom comparator sorting using `qsort()`.
---

## Project Overview
In this assignment, you will build a command-line utility called `gradesorter`. The program reads student grade records in CSV format, computes an **Average** column for each student, sorts the records based on command-line flags, and outputs the formatted results.

---

## Program Specifications & Command-Line Interface (CLI)

Your program must parse command-line arguments using `getopt(3)`.

### Syntax
```bash
./gradesorter [-i input_file] [-o output_file] [-s column] [-r] [-z]
```

### Options & Flags

| Flag | Argument | Description | Default Behavior (if flag omitted) |
| :--- | :--- | :--- | :--- |
| **`-i`** | `input_file` | Specifies the path to the input CSV file. | Read input from **`stdin`** |
| **`-o`** | `output_file` | Specifies the path to the output file. | Write results to **`stdout`** |
| **`-s`** | `column` | Column name (string) or 1-based index (integer) to sort by. | Sort by the **`Average`** column |
| **`-r`** | *None* | Reverses the sort order (Ascending scores instead of Descending). | High-to-Low score sorting (Descending) |
| **`-z`** | *None* | Includes students with missing values (`NaN`) in the output. | **Exclude/Filter out** students missing the target score |

---

## CSV File Format

1. **Header Row:** The input CSV file begins with a header row containing column names (e.g., `Name,Quiz 1,Quiz 2,Exam 1`).
2. **Data Rows:** Each subsequent line contains a student's name followed by numerical scores separated by commas.
   - Names may contain spaces and alphanumeric characters (no commas).
   - Scores are numbers with optional decimal points.
   - **Missing Values:** Scores can be omitted (e.g., `Alice,,85` or `Bob,90,`).
3. **Dynamic Average Column:** Your program must automatically compute a virtual **`Average`** column for each student, calculated as the mean of all available non-empty numerical scores for that student.

---

## Sorting Rules & -z / -r Interactions

1. **Column Selection (`-s column`):**
   - If `column` is an exact case-sensitive match for a CSV header (or `"Average"`), sort by that column.
   - If `column` is an integer $N \ge 1$, treat it as the 1-based column index (Column 1 is typically Name, Column 2 is first score, etc.).
   - If `-s` is omitted, default to sorting by **`Average`**.
2. **Score Order:**
   - **Default (without `-r`):** Sort scores from **highest to lowest** (descending).
   - **With `-r`:** Sort scores from **lowest to highest** (ascending).
3. **Tie-Breaking:**
   - If two or more students have identical scores in the target column, break ties **lexicographically by name in ascending order (`A-Z`)**.
4. **Handling Missing Values (`-z`):**
   - **Without `-z`:** Exclude any student who lacks a score for the chosen column from the final output.
   - **With `-z`:** Include students missing the target score in the output. Missing scores print as `nan`.
   - **Positioning of `NaN`:** Missing scores (`NaN`) are **always placed at the bottom** of the output list regardless of whether `-r` is active. Multiple missing-score entries at the bottom are ordered lexicographically by name (`A-Z`).

---

## Formatting Requirements

Output each student record in the following format:
```text
<Student Name> score: <Score>
```
- Print valid numerical scores with **2 decimal places** of precision (`%.2f`).
- Print missing scores as **`nan`**.

### Example Output
```text
Alan Turing score: 94.33
Barbara Johns score: 84.42
Viet Tran score: 81.00
Jang Yeong-sil score: nan
```

---

## Error Handling & Exit Codes

Your program must exit with **return code `1`** and print a descriptive error message to `stderr` under any of the following conditions:
- Invalid or unrecognized command-line option.
- Target column specified by `-s` does not exist in the header.
- Unable to open input file or output file.
- Malformed CSV file structure.

For normal successful execution, exit with **return code `0`**.

---

## Technical Constraints & Compilation

- **Source File:** All logic must reside in `gradesorter.c`.
- **Compilation Flags:** You MUST compile using the following mandatory flags:
  ```bash
  gcc -O2 -Wall -Wextra -Werror -pedantic -std=c17 gradesorter.c -o gradesorter
  ```
- **Memory Management:** You must dynamically allocate memory (`malloc`/`realloc`/`free`) for line buffers, student arrays, and columns. **No static limits** on student count, name lengths, or number of scores. Check all dynamic memory allocations for allocation failures.
- **Resource Cleanup:** Ensure all allocated memory is freed and all opened file streams (`fopen`/`fclose`) are closed before program exit.

---

## Submission Checklist
1. Ensure `gradesorter.c` compiles with zero warnings or errors under the specified gcc flags.
2. Provide a `Makefile` that compiles `gradesorter` with a simple `make` command.
