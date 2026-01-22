# Using the Raspberry Pi Imager JSON Schema

This guide explains how to create, validate, and manipulate Raspberry Pi Imager OS list JSON files using the provided JSON Schema in various programming languages and command-line tools.

## Table of Contents

- [Overview](#overview)
- [Schema Structure](#schema-structure)
- [Validation](#validation)
  - [Python](#python-validation)
  - [C++](#c-validation)
  - [C](#c-validation)
  - [Rust](#rust-validation)
- [Creating OS Lists](#creating-os-lists)
  - [Python](#python-creation)
  - [C++](#c-creation)
  - [C](#c-creation)
  - [Rust](#rust-creation)
- [Command-Line Tools](#command-line-tools)
  - [Using jq](#using-jq)
  - [Using jsonschema (CLI)](#using-jsonschema-cli)
  - [Shell Scripts](#shell-scripts)
- [Best Practices](#best-practices)
- [Common Pitfalls](#common-pitfalls)
- [Additional Resources](#additional-resources)
- [Testing Your JSON Files](#testing-your-json-files)
- [Requesting an addition to the official Raspberry Pi Imager OS List](#requesting-an-addition-to-the-official-raspberry-pi-imager-os-list)

## Overview

The `os-list-schema.json` file defines the structure for Raspberry Pi Imager's OS list files using [JSON Schema Draft 07](https://json-schema.org/draft-07/json-schema-release-notes.html). These files control which operating systems appear in the Imager application and their metadata.

**Schema Location:** `doc/json-schema/os-list-schema.json`  
**Example File:** `doc/os-sublist-example.json`

## Schema Structure

The schema defines two main components:

1. **`imager`** (optional): Top-level metadata for the Imager application itself
2. **`os_list`** (required): Array of operating system entries

Each OS entry must include:

- `name`: Display name
- `description`: Description text
- `icon`: Icon URL or path
- `url`: Download URL for the OS image
- `extract_size`: Size in bytes after decompression
- `extract_sha256`: SHA256 hash of extracted image (critical for security)
- `image_download_size`: Compressed download size
- `release_date`: Release date (YYYY-MM-DD format)
- `devices`: Array of compatible device identifiers

## Validation

### Python Validation

**Install dependencies:**

```bash
pip install jsonschema
```

**Basic validation:**

```python
#!/usr/bin/env python3
import json
import jsonschema
from pathlib import Path

def validate_os_list(schema_path, json_path):
    """Validate an OS list JSON file against the schema."""
    # Load schema
    with open(schema_path) as f:
        schema = json.load(f)
    
    # Load OS list
    with open(json_path) as f:
        os_list = json.load(f)
    
    # Validate
    try:
        jsonschema.validate(instance=os_list, schema=schema)
        print(f"✓ {json_path} is valid")
        return True
    except jsonschema.exceptions.ValidationError as e:
        print(f"✗ Validation error in {json_path}:")
        print(f"  Path: {'.'.join(str(p) for p in e.path)}")
        print(f"  Error: {e.message}")
        return False
    except jsonschema.exceptions.SchemaError as e:
        print(f"✗ Schema error: {e.message}")
        return False

# Usage
if __name__ == "__main__":
    validate_os_list(
        "doc/json-schema/os-list-schema.json",
        "doc/os-sublist-example.json"
    )
```

### C++ Validation

**Using [nlohmann/json](https://github.com/nlohmann/json) with [json-schema-validator](https://github.com/pboettch/json-schema-validator):**

```cpp
#include <iostream>
#include <fstream>
#include <nlohmann/json.hpp>
#include <nlohmann/json-schema.hpp>

using json = nlohmann::json;
using json_validator = nlohmann::json_schema::json_validator;

bool validate_os_list(const std::string& schema_path, 
                      const std::string& json_path) {
    try {
        // Load schema
        std::ifstream schema_file(schema_path);
        json schema = json::parse(schema_file);
        
        // Create validator
        json_validator validator;
        validator.set_root_schema(schema);
        
        // Load and validate JSON
        std::ifstream json_file(json_path);
        json os_list = json::parse(json_file);
        
        validator.validate(os_list);
        std::cout << "✓ " << json_path << " is valid" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "✗ Validation error: " << e.what() << std::endl;
        return false;
    }
}

int main() {
    validate_os_list(
        "doc/json-schema/os-list-schema.json",
        "doc/os-sublist-example.json"
    );
    return 0;
}
```

**Build with CMake:**

```cmake
find_package(nlohmann_json REQUIRED)
add_executable(validate_os_list validate.cpp)
target_link_libraries(validate_os_list nlohmann_json::nlohmann_json)
```

### C Validation

**Using [json-c](https://github.com/json-c/json-c):**

Note: C doesn't have native JSON Schema validation. You'll need to manually check fields or use a wrapper around a C++ library.

```c
#include <stdio.h>
#include <json-c/json.h>

int validate_required_fields(struct json_object *os_entry) {
    const char *required[] = {
        "name", "description", "icon", "url",
        "extract_size", "extract_sha256",
        "image_download_size", "release_date", "devices"
    };
    
    for (int i = 0; i < 9; i++) {
        struct json_object *field;
        if (!json_object_object_get_ex(os_entry, required[i], &field)) {
            fprintf(stderr, "Missing required field: %s\n", required[i]);
            return 0;
        }
    }
    return 1;
}

int validate_os_list(const char *json_path) {
    struct json_object *root, *os_list;
    
    // Parse JSON file
    root = json_object_from_file(json_path);
    if (!root) {
        fprintf(stderr, "Failed to parse JSON file\n");
        return 0;
    }
    
    // Check for os_list array
    if (!json_object_object_get_ex(root, "os_list", &os_list)) {
        fprintf(stderr, "Missing required 'os_list' field\n");
        json_object_put(root);
        return 0;
    }
    
    if (!json_object_is_type(os_list, json_type_array)) {
        fprintf(stderr, "'os_list' must be an array\n");
        json_object_put(root);
        return 0;
    }
    
    // Validate each OS entry
    size_t n_entries = json_object_array_length(os_list);
    for (size_t i = 0; i < n_entries; i++) {
        struct json_object *entry = json_object_array_get_idx(os_list, i);
        if (!validate_required_fields(entry)) {
            fprintf(stderr, "Validation failed for entry %zu\n", i);
            json_object_put(root);
            return 0;
        }
    }
    
    printf("✓ %s is valid\n", json_path);
    json_object_put(root);
    return 1;
}

int main() {
    return validate_os_list("doc/os-sublist-example.json") ? 0 : 1;
}
```

**Compile:**

```bash
gcc -o validate validate.c -ljson-c
```

### Rust Validation

**Using [serde_json](https://github.com/serde-rs/json) and [jsonschema](https://github.com/Stranger6667/jsonschema-rs):**

**Cargo.toml:**

```toml
[dependencies]
serde = { version = "1.0", features = ["derive"] }
serde_json = "1.0"
jsonschema = "0.17"
```

**Code:**

```rust
use std::fs;
use serde_json::Value;
use jsonschema::JSONSchema;

fn validate_os_list(schema_path: &str, json_path: &str) -> Result<(), Box<dyn std::error::Error>> {
    // Load schema
    let schema_content = fs::read_to_string(schema_path)?;
    let schema: Value = serde_json::from_str(&schema_content)?;
    
    // Compile schema
    let compiled = JSONSchema::compile(&schema)
        .map_err(|e| format!("Schema compilation failed: {}", e))?;
    
    // Load and validate JSON
    let json_content = fs::read_to_string(json_path)?;
    let instance: Value = serde_json::from_str(&json_content)?;
    
    match compiled.validate(&instance) {
        Ok(_) => {
            println!("✓ {} is valid", json_path);
            Ok(())
        }
        Err(errors) => {
            eprintln!("✗ Validation errors in {}:", json_path);
            for error in errors {
                eprintln!("  - {}", error);
                eprintln!("    Instance path: {}", error.instance_path);
            }
            Err("Validation failed".into())
        }
    }
}

fn main() {
    if let Err(e) = validate_os_list(
        "doc/json-schema/os-list-schema.json",
        "doc/os-sublist-example.json"
    ) {
        eprintln!("Error: {}", e);
        std::process::exit(1);
    }
}
```

**Build and run:**

```bash
cargo build --release
./target/release/validate_os_list
```

## Creating OS Lists

### Python Creation

```python
#!/usr/bin/env python3
import json
import hashlib
from datetime import datetime
from pathlib import Path

class OSListBuilder:
    def __init__(self):
        self.data = {"os_list": []}
    
    def add_os(self, name, description, image_path, icon_url, 
               url, devices, **kwargs):
        """Add an OS entry with automatic hash calculation."""
        
        # Calculate SHA256 of the image
        sha256_hash = hashlib.sha256()
        with open(image_path, 'rb') as f:
            for chunk in iter(lambda: f.read(4096), b""):
                sha256_hash.update(chunk)
        
        extract_size = Path(image_path).stat().st_size
        
        entry = {
            "name": name,
            "description": description,
            "icon": icon_url,
            "url": url,
            "extract_size": extract_size,
            "extract_sha256": sha256_hash.hexdigest(),
            "image_download_size": kwargs.get('download_size', extract_size),
            "release_date": kwargs.get('release_date', 
                                      datetime.now().strftime('%Y-%m-%d')),
            "devices": devices
        }
        
        # Optional fields
        if 'init_format' in kwargs:
            entry['init_format'] = kwargs['init_format']
        if 'website' in kwargs:
            entry['website'] = kwargs['website']
        if 'architecture' in kwargs:
            entry['architecture'] = kwargs['architecture']
        if 'capabilities' in kwargs:
            entry['capabilities'] = kwargs['capabilities']
        
        self.data['os_list'].append(entry)
        return self
    
    def set_imager_metadata(self, latest_version, url, **kwargs):
        """Set top-level imager metadata."""
        self.data['imager'] = {
            "latest_version": latest_version,
            "url": url
        }
        for key in ['default_os', 'embedded_default_os', 
                    'embedded_default_destination']:
            if key in kwargs:
                self.data['imager'][key] = kwargs[key]
        return self
    
    def save(self, output_path):
        """Save to JSON file."""
        with open(output_path, 'w') as f:
            json.dump(self.data, f, indent=2)
        print(f"✓ Saved to {output_path}")

# Usage example
if __name__ == "__main__":
    builder = OSListBuilder()
    builder.add_os(
        name="My Custom OS",
        description="A custom operating system for Raspberry Pi",
        image_path="./my-os.img",
        icon_url="https://example.com/icon.png",
        url="https://example.com/my-os.img.xz",
        devices=["pi4", "pi5"],
        init_format="systemd",
        architecture="armv8",
        capabilities=["i2c", "spi", "secure_boot"]
    )
    builder.save('my-os-list.json')
```

### C++ Creation

```cpp
#include <iostream>
#include <fstream>
#include <nlohmann/json.hpp>
#include <openssl/sha.h>
#include <iomanip>
#include <sstream>

using json = nlohmann::json;

class OSListBuilder {
private:
    json data;
    
    std::string calculate_sha256(const std::string& path) {
        std::ifstream file(path, std::ios::binary);
        SHA256_CTX sha256;
        SHA256_Init(&sha256);
        
        char buffer[4096];
        while (file.read(buffer, sizeof(buffer)) || file.gcount() > 0) {
            SHA256_Update(&sha256, buffer, file.gcount());
        }
        
        unsigned char hash[SHA256_DIGEST_LENGTH];
        SHA256_Final(hash, &sha256);
        
        std::stringstream ss;
        for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
            ss << std::hex << std::setw(2) << std::setfill('0') 
               << static_cast<int>(hash[i]);
        }
        return ss.str();
    }
    
public:
    OSListBuilder() {
        data["os_list"] = json::array();
    }
    
    OSListBuilder& add_os(const std::string& name,
                         const std::string& description,
                         const std::string& image_path,
                         const std::string& icon_url,
                         const std::string& url,
                         const std::vector<std::string>& devices) {
        json entry = {
            {"name", name},
            {"description", description},
            {"icon", icon_url},
            {"url", url},
            {"extract_sha256", calculate_sha256(image_path)},
            {"devices", devices},
            {"release_date", "2024-01-01"}  // Use actual date
        };
        
        // Get file size
        std::ifstream file(image_path, std::ios::binary | std::ios::ate);
        size_t size = file.tellg();
        entry["extract_size"] = size;
        entry["image_download_size"] = size;
        
        data["os_list"].push_back(entry);
        return *this;
    }
    
    OSListBuilder& set_init_format(size_t index, const std::string& format) {
        data["os_list"][index]["init_format"] = format;
        return *this;
    }
    
    OSListBuilder& set_architecture(size_t index, const std::string& arch) {
        data["os_list"][index]["architecture"] = arch;
        return *this;
    }
    
    OSListBuilder& set_capabilities(size_t index, 
                                   const std::vector<std::string>& caps) {
        data["os_list"][index]["capabilities"] = caps;
        return *this;
    }
    
    void save(const std::string& output_path) {
        std::ofstream file(output_path);
        file << std::setw(2) << data << std::endl;
        std::cout << "✓ Saved to " << output_path << std::endl;
    }
};

int main() {
    OSListBuilder builder;
    builder.add_os(
        "My Custom OS",
        "A custom operating system",
        "./my-os.img",
        "https://example.com/icon.png",
        "https://example.com/my-os.img.xz",
        {"pi4", "pi5"}
    )
    .set_init_format(0, "systemd")
    .set_architecture(0, "armv8")
    .set_capabilities(0, {"i2c", "spi", "secure_boot"})
    .save("my-os-list.json");
    
    return 0;
}
```

### C Creation

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/sha.h>
#include <json-c/json.h>
#include <sys/stat.h>

char* calculate_sha256(const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file) return NULL;
    
    SHA256_CTX sha256;
    SHA256_Init(&sha256);
    
    unsigned char buffer[4096];
    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        SHA256_Update(&sha256, buffer, bytes_read);
    }
    fclose(file);
    
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_Final(hash, &sha256);
    
    char *hex = malloc(SHA256_DIGEST_LENGTH * 2 + 1);
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        sprintf(hex + (i * 2), "%02x", hash[i]);
    }
    hex[SHA256_DIGEST_LENGTH * 2] = '\0';
    return hex;
}

long get_file_size(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        return st.st_size;
    }
    return -1;
}

struct json_object* create_os_entry(
    const char *name,
    const char *description,
    const char *image_path,
    const char *icon_url,
    const char *url,
    const char **devices,
    int num_devices
) {
    struct json_object *entry = json_object_new_object();
    
    // Required fields
    json_object_object_add(entry, "name", json_object_new_string(name));
    json_object_object_add(entry, "description", json_object_new_string(description));
    json_object_object_add(entry, "icon", json_object_new_string(icon_url));
    json_object_object_add(entry, "url", json_object_new_string(url));
    
    // Calculate hash and size
    char *sha256 = calculate_sha256(image_path);
    long size = get_file_size(image_path);
    
    json_object_object_add(entry, "extract_sha256", json_object_new_string(sha256));
    json_object_object_add(entry, "extract_size", json_object_new_int64(size));
    json_object_object_add(entry, "image_download_size", json_object_new_int64(size));
    json_object_object_add(entry, "release_date", json_object_new_string("2024-01-01"));
    
    free(sha256);
    
    // Devices array
    struct json_object *devices_array = json_object_new_array();
    for (int i = 0; i < num_devices; i++) {
        json_object_array_add(devices_array, json_object_new_string(devices[i]));
    }
    json_object_object_add(entry, "devices", devices_array);
    
    return entry;
}

int main() {
    struct json_object *root = json_object_new_object();
    struct json_object *os_list = json_object_new_array();
    
    const char *devices[] = {"pi4", "pi5"};
    struct json_object *entry = create_os_entry(
        "My Custom OS",
        "A custom operating system",
        "./my-os.img",
        "https://example.com/icon.png",
        "https://example.com/my-os.img.xz",
        devices,
        2
    );
    
    // Add optional fields
    json_object_object_add(entry, "init_format", json_object_new_string("systemd"));
    json_object_object_add(entry, "architecture", json_object_new_string("armv8"));
    
    json_object_array_add(os_list, entry);
    json_object_object_add(root, "os_list", os_list);
    
    // Save to file
    if (json_object_to_file_ext("my-os-list.json", root, 
                                JSON_C_TO_STRING_PRETTY) == 0) {
        printf("✓ Saved to my-os-list.json\n");
    } else {
        fprintf(stderr, "✗ Failed to save file\n");
    }
    
    json_object_put(root);
    return 0;
}
```

### Rust Creation

```rust
use serde::{Deserialize, Serialize};
use serde_json;
use sha2::{Sha256, Digest};
use std::fs::{self, File};
use std::io::{self, Read};

#[derive(Serialize, Deserialize)]
struct OSList {
    os_list: Vec<OSEntry>,
}

#[derive(Serialize, Deserialize)]
struct OSEntry {
    name: String,
    description: String,
    icon: String,
    url: String,
    extract_size: u64,
    extract_sha256: String,
    image_download_size: u64,
    release_date: String,
    devices: Vec<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    init_format: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    architecture: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    capabilities: Option<Vec<String>>,
    #[serde(skip_serializing_if = "Option::is_none")]
    website: Option<String>,
}

fn calculate_sha256(path: &str) -> io::Result<String> {
    let mut file = File::open(path)?;
    let mut hasher = Sha256::new();
    let mut buffer = [0u8; 4096];
    
    loop {
        let bytes_read = file.read(&mut buffer)?;
        if bytes_read == 0 {
            break;
        }
        hasher.update(&buffer[..bytes_read]);
    }
    
    Ok(format!("{:x}", hasher.finalize()))
}

struct OSListBuilder {
    os_list: Vec<OSEntry>,
}

impl OSListBuilder {
    fn new() -> Self {
        OSListBuilder { os_list: Vec::new() }
    }
    
    fn add_os(&mut self, 
              name: &str,
              description: &str,
              image_path: &str,
              icon_url: &str,
              url: &str,
              devices: Vec<String>) -> io::Result<&mut Self> {
        let metadata = fs::metadata(image_path)?;
        let size = metadata.len();
        let sha256 = calculate_sha256(image_path)?;
        
        let entry = OSEntry {
            name: name.to_string(),
            description: description.to_string(),
            icon: icon_url.to_string(),
            url: url.to_string(),
            extract_size: size,
            extract_sha256: sha256,
            image_download_size: size,
            release_date: chrono::Local::now().format("%Y-%m-%d").to_string(),
            devices,
            init_format: None,
            architecture: None,
            capabilities: None,
            website: None,
        };
        
        self.os_list.push(entry);
        Ok(self)
    }
    
    fn set_init_format(&mut self, index: usize, format: &str) -> &mut Self {
        if let Some(entry) = self.os_list.get_mut(index) {
            entry.init_format = Some(format.to_string());
        }
        self
    }
    
    fn set_architecture(&mut self, index: usize, arch: &str) -> &mut Self {
        if let Some(entry) = self.os_list.get_mut(index) {
            entry.architecture = Some(arch.to_string());
        }
        self
    }
    
    fn set_capabilities(&mut self, index: usize, caps: Vec<String>) -> &mut Self {
        if let Some(entry) = self.os_list.get_mut(index) {
            entry.capabilities = Some(caps);
        }
        self
    }
    
    fn save(&self, output_path: &str) -> io::Result<()> {
        let os_list = OSList {
            os_list: self.os_list.clone(),
        };
        
        let json = serde_json::to_string_pretty(&os_list)?;
        fs::write(output_path, json)?;
        println!("✓ Saved to {}", output_path);
        Ok(())
    }
}

fn main() -> io::Result<()> {
    let mut builder = OSListBuilder::new();
    builder
        .add_os(
            "My Custom OS",
            "A custom operating system",
            "./my-os.img",
            "https://example.com/icon.png",
            "https://example.com/my-os.img.xz",
            vec!["pi4".to_string(), "pi5".to_string()],
        )?
        .set_init_format(0, "systemd")
        .set_architecture(0, "armv8")
        .set_capabilities(0, vec![
            "i2c".to_string(),
            "spi".to_string(),
            "secure_boot".to_string(),
        ])
        .save("my-os-list.json")?;
    
    Ok(())
}
```

**Additional Cargo.toml dependencies:**

```toml
[dependencies]
serde = { version = "1.0", features = ["derive"] }
serde_json = "1.0"
sha2 = "0.10"
chrono = "0.4"
```

## Command-Line Tools

### Using jq

[jq](https://stedolan.github.io/jq/) is a powerful command-line JSON processor.

**Basic validation (check structure):**

```bash
# Check if file is valid JSON and has os_list
jq -e '.os_list | length' doc/os-sublist-example.json
```

**Extract all OS names:**

```bash
jq -r '.os_list[].name' doc/os-sublist-example.json
```

**Add a new OS entry:**

```bash
jq '.os_list += [{
  "name": "New OS",
  "description": "A new operating system",
  "icon": "https://example.com/icon.png",
  "url": "https://example.com/new-os.img.xz",
  "extract_size": 4294967296,
  "extract_sha256": "abc123...",
  "image_download_size": 1073741824,
  "release_date": "2024-01-15",
  "devices": ["pi4", "pi5"],
  "init_format": "systemd"
}]' my-os-list.json > updated-os-list.json
```

**Update an OS entry by name:**

```bash
jq '(.os_list[] | select(.name == "Your Operating System Name") | .release_date) = "2024-02-01"' \
  doc/os-sublist-example.json > updated.json
```

**Filter OS entries by device support:**

```bash
jq '.os_list[] | select(.devices | contains(["pi4"]))' my-os-list.json
```

**Get total download size:**

```bash
jq '[.os_list[].image_download_size] | add' my-os-list.json
```

**Check for required fields:**

```bash
jq -e '.os_list[] | 
  select(
    has("name") and 
    has("description") and 
    has("icon") and 
    has("url") and 
    has("extract_size") and 
    has("extract_sha256") and 
    has("image_download_size") and 
    has("release_date") and 
    has("devices")
  ) | .name' my-os-list.json
```

**Pretty-print with colors:**

```bash
jq -C '.' my-os-list.json | less -R
```

**Compact output (no formatting):**

```bash
jq -c '.' my-os-list.json
```

**Sort OS list by release date:**

```bash
jq '.os_list |= sort_by(.release_date)' my-os-list.json
```

**Add architecture to all entries:**

```bash
jq '.os_list[] |= . + {"architecture": "armv8"}' my-os-list.json
```

**Remove optional fields:**

```bash
jq '.os_list[] |= del(.website, .init_format)' my-os-list.json
```

### Using jsonschema (CLI)

**Install:**

```bash
pip install jsonschema
```

**Validate from command line:**

```bash
jsonschema -i doc/os-sublist-example.json doc/json-schema/os-list-schema.json
```

**Or use the Python sort-of-one-liner:**

```bash
python3 -c "
import json, jsonschema, sys
schema = json.load(open('doc/json-schema/os-list-schema.json'))
data = json.load(open('doc/os-sublist-example.json'))
try:
    jsonschema.validate(data, schema)
    print('✓ Valid')
except Exception as e:
    print(f'✗ {e}')
    sys.exit(1)
"
```

### Shell Scripts

**Complete validation and creation script:**

```bash
#!/bin/bash
# create-os-list.sh - Create an OS list entry

set -e

IMAGE_FILE="$1"
OS_NAME="$2"
OS_DESC="$3"
ICON_URL="$4"
DOWNLOAD_URL="$5"
DEVICES="$6"  # Comma-separated, e.g., "pi4,pi5"

if [ "$#" -lt 6 ]; then
    echo "Usage: $0 <image_file> <name> <description> <icon_url> <download_url> <devices>"
    exit 1
fi

# Calculate SHA256
echo "Calculating SHA256..."
SHA256=$(sha256sum "$IMAGE_FILE" | awk '{print $1}')

# Get file size
SIZE=$(stat -f%z "$IMAGE_FILE" 2>/dev/null || stat -c%s "$IMAGE_FILE")

# Get current date
DATE=$(date +%Y-%m-%d)

# Convert comma-separated devices to JSON array
DEVICES_JSON=$(echo "$DEVICES" | jq -R 'split(",") | map(gsub("^\\s+|\\s+$";""))')

# Create JSON entry
ENTRY=$(jq -n \
  --arg name "$OS_NAME" \
  --arg desc "$OS_DESC" \
  --arg icon "$ICON_URL" \
  --arg url "$DOWNLOAD_URL" \
  --argjson size "$SIZE" \
  --arg sha "$SHA256" \
  --arg date "$DATE" \
  --argjson devices "$DEVICES_JSON" \
  '{
    name: $name,
    description: $desc,
    icon: $icon,
    url: $url,
    extract_size: $size,
    extract_sha256: $sha,
    image_download_size: $size,
    release_date: $date,
    devices: $devices
  }')

# Add to existing list or create new one
OUTPUT_FILE="os-list.json"
if [ -f "$OUTPUT_FILE" ]; then
    jq --argjson entry "$ENTRY" '.os_list += [$entry]' "$OUTPUT_FILE" > "${OUTPUT_FILE}.tmp"
    mv "${OUTPUT_FILE}.tmp" "$OUTPUT_FILE"
else
    echo "{\"os_list\": [$ENTRY]}" | jq '.' > "$OUTPUT_FILE"
fi

echo "✓ Added $OS_NAME to $OUTPUT_FILE"
echo "✓ SHA256: $SHA256"
echo "✓ Size: $SIZE bytes"
```

**Usage:**

```bash
chmod +x create-os-list.sh
./create-os-list.sh my-os.img "My OS" "Custom OS for Pi" \
  "https://example.com/icon.png" \
  "https://example.com/my-os.img.xz" \
  "pi4,pi5"
```

**Batch validation script:**

```bash
#!/bin/bash
# validate-all.sh - Validate all JSON files against schema

SCHEMA="doc/json-schema/os-list-schema.json"

for json_file in *.json; do
    echo -n "Validating $json_file... "
    if python3 -c "
import json, jsonschema, sys
try:
    schema = json.load(open('$SCHEMA'))
    data = json.load(open('$json_file'))
    jsonschema.validate(data, schema)
    print('✓')
except Exception as e:
    print(f'✗ {e}')
    sys.exit(1)
    "; then
        continue
    else
        echo "Validation failed for $json_file"
        exit 1
    fi
done

echo "All files validated successfully!"
```

## Best Practices

1. **Always Validate SHA256 Hashes**
   - The `extract_sha256` is critical for security
   - Imager will refuse to write images with incorrect hashes
   - Hash mismatches invalidate the partition table

2. **Use Correct Date Format**
   - Always use ISO 8601 format: `YYYY-MM-DD`
   - Example: `2024-01-15`

3. **Specify Device Compatibility**
   - The `devices` array is required
   - Use official device identifiers (see schema examples)
   - Consider using `matching_type` for flexible matching

4. **Enable OS Customization**
   - Set `init_format` to enable the customization wizard
   - Use `"systemd"` for most Raspberry Pi OS derivatives
   - Use `"cloudinit"` or `"cloudinit-rpi"` for cloud-init based systems
   - Use `"none"` or `""` to explicitly disable

5. **Compressed Image Formats**
   - Supported: `.zip`, `.gz`, `.xz`, `.zstd` (Imager 1.7+)
   - For backward compatibility, prefer `.xz` over `.zstd`

6. **Use External Subitems**
   - Prefer `subitems_url` over inline `subitems`
   - Inline subitems had bugs in Imager versions before 1.7

7. **Icon Guidelines**
   - Recommend 40x40 pixels
   - SVG or PNG format
   - Can be local paths or URLs

8. **Architecture and Capabilities**
   - Set `architecture` for proper sorting on device selection
   - Use `capabilities` to enable hardware-specific features
   - Interface capabilities (i2c, spi, etc.) need both device and OS support

## Common Pitfalls

### 1. **Missing Required Fields**

❌ Wrong:

```json
{
  "os_list": [{
    "name": "My OS",
    "url": "https://example.com/os.img.xz"
  }]
}
```

✅ Correct:

```json
{
  "os_list": [{
    "name": "My OS",
    "description": "Description here",
    "icon": "https://example.com/icon.png",
    "url": "https://example.com/os.img.xz",
    "extract_size": 4294967296,
    "extract_sha256": "abc123...",
    "image_download_size": 1073741824,
    "release_date": "2024-01-15",
    "devices": ["pi4", "pi5"]
  }]
}
```

### 2. **Wrong SHA256 Hash**

❌ Don't hash the compressed file for `extract_sha256`:

```bash
sha256sum my-os.img.xz  # Wrong for extract_sha256!
```

✅ Hash the extracted/uncompressed image:

```bash
xz -d -c my-os.img.xz | sha256sum
# OR
sha256sum my-os.img
```

### 3. **Incorrect Size Units**

The sizes must be in **bytes**, not megabytes or gigabytes:

❌ Wrong:

```json
"extract_size": 4096  // This is 4KB, probably wrong!
```

✅ Correct:

```json
"extract_size": 4294967296  // 4GB in bytes
```

### 4. **Invalid init_format Values**

❌ Wrong:

```json
"init_format": "system-d"  // Typo!
```

✅ Correct (must be one of these):

```json
"init_format": "systemd"
"init_format": "cloudinit"
"init_format": "cloudinit-rpi"
"init_format": "none"
"init_format": ""  // Empty string to disable
```

### 5. **Empty Device Arrays**

❌ Wrong:

```json
"devices": []  // Empty array
```

✅ Correct:

```json
"devices": ["pi4", "pi5"]  // At least one device
```

### 6. **Top-Level Imager Metadata in Sublists**

The `imager` object should **only** appear in the top-level OS list, not in files referenced by `subitems_url`:

❌ Wrong (in a sublist file):

```json
{
  "imager": { "latest_version": "1.8" },
  "os_list": [...]
}
```

✅ Correct (in sublist file):

```json
{
  "os_list": [...]
}
```

## Additional Resources

- **JSON Schema Official Documentation:** https://json-schema.org/
- **jq Manual:** https://stedolan.github.io/jq/manual/
- **Raspberry Pi Imager Source:** https://github.com/raspberrypi/rpi-imager
- **Schema Validation Tools:**
  - Python: `jsonschema` library
  - JavaScript: `ajv` library
  - Go: `gojsonschema` library
  - Rust: `jsonschema` crate
  - C++: `json-schema-validator` library

## Testing Your JSON Files

Before deploying your OS list JSON:

1. **Validate against schema** using one of the methods above
2. **Test with actual Imager application**
3. **Verify download URLs are accessible**
4. **Confirm SHA256 hashes match**
5. **Test OS customization** if `init_format` is set
6. **Check icon displays properly** (40x40 recommended)

## Requesting an addition to the official Raspberry Pi Imager OS List

You will need to write a sublist JSON, and test it prior to submission:

General principles you should follow:

1. Follow the schema strictly
2. Test with multiple Imager versions
3. Provide accurate metadata
4. Use appropriate compression
5. Host images on reliable infrastructure
6. Keep icons lightweight and properly sized

With those principles in mind, you can then fill out the form: https://forms.gle/U6JkxVfhHkn5ok2X7

## License

This documentation follows the same license as the Raspberry Pi Imager project.
