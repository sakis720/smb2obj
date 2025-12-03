#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <sstream>
#include <iomanip>
#include <algorithm>

// Half-float conversion
float readHalfFloat(const uint8_t* data, size_t offset) {
    uint16_t uint16;
    std::memcpy(&uint16, data + offset, 2);
    
    int sign = (uint16 & 0x8000) >> 15;
    int exponent = (uint16 & 0x7C00) >> 10;
    int fraction = uint16 & 0x03FF;
    
    if (exponent == 0) {
        return (sign ? -1.0f : 1.0f) * std::pow(2.0f, -14.0f) * (fraction / 1024.0f);
    }
    if (exponent == 0x1F) {
        return fraction ? NAN : (sign ? -INFINITY : INFINITY);
    }
    return (sign ? -1.0f : 1.0f) * std::pow(2.0f, float(exponent - 15)) * (1.0f + fraction / 1024.0f);
}

struct SubmeshInfo {
    std::string name;
    std::string materialName;
    uint32_t vertexCount;
    uint32_t faceCount;
    uint32_t stride;
};

struct Vec3 {
    float x, y, z;
};

struct Vec2 {
    float u, v;
};

struct Face {
    uint16_t i1, i2, i3;
};

struct SubmeshData {
    std::string name;
    std::string materialName;
    std::vector<Vec3> vertices;
    std::vector<Vec3> normals;
    std::vector<Vec2> uvs;
    std::vector<Face> faces;
};

class SMBConverter {
private:
    std::vector<uint8_t> fileData;
    std::vector<std::string> materials;
    std::vector<SubmeshInfo> submeshes;
    
public:
    bool loadFile(const std::string& filepath) {
        std::ifstream file(filepath, std::ios::binary);
        if (!file) {
            std::cerr << "Error: Could not open file: " << filepath << std::endl;
            return false;
        }
        
        file.seekg(0, std::ios::end);
        size_t fileSize = file.tellg();
        file.seekg(0, std::ios::beg);
        
        fileData.resize(fileSize);
        file.read(reinterpret_cast<char*>(fileData.data()), fileSize);
        file.close();
        
        std::cout << "Loaded file: " << filepath << " (" << fileSize << " bytes)" << std::endl;
        return true;
    }
    
    bool parseHeader() {
        if (fileData.size() < 64) {
            std::cerr << "Error: File too small to contain valid header" << std::endl;
            return false;
        }
        
        size_t offset = 0;
        
        // Skip initial header (40 bytes)
        offset += 40;
        
        // Read counts
        uint32_t submeshCount, collisionMeshCount, tagCount, materialsCount;
        std::memcpy(&submeshCount, &fileData[offset], 4); offset += 4;
        std::memcpy(&collisionMeshCount, &fileData[offset], 4); offset += 4;
        std::memcpy(&tagCount, &fileData[offset], 4); offset += 4;
        std::memcpy(&materialsCount, &fileData[offset], 4); offset += 4;
        offset += 8; // Skip remaining header data
        
        std::cout << "Submeshes: " << submeshCount << ", Materials: " << materialsCount << std::endl;
        
        // Parse materials
        for (uint32_t i = 0; i < materialsCount; i++) {
            size_t nameEnd = offset;
            while (nameEnd < fileData.size() && fileData[nameEnd] != 0) {
                nameEnd++;
            }
            
            std::string materialName(reinterpret_cast<char*>(&fileData[offset]), nameEnd - offset);
            materials.push_back(materialName);
            
            size_t nameLength = nameEnd - offset + 1;
            size_t paddedLength = ((nameLength + 3) / 4) * 4;
            offset += paddedLength;
        }
        
        // Skip tags/collision data
        if (tagCount > 0) {
            offset += (tagCount * 48) + 24;
        } else if (collisionMeshCount > 0) {
            offset += collisionMeshCount * 428 + 24;
        } else {
            offset += 24;
        }
        
        // Parse submesh headers
        for (uint32_t i = 0; i < submeshCount; i++) {
            size_t headerStart = offset;
            
            // Read submesh name (first 48 bytes)
            size_t nameEnd = offset;
            while (nameEnd < offset + 48 && nameEnd < fileData.size() && fileData[nameEnd] != 0) {
                nameEnd++;
            }
            std::string submeshName(reinterpret_cast<char*>(&fileData[offset]), nameEnd - offset);
            
            // Read material index (at +48)
            uint32_t materialIdx;
            std::memcpy(&materialIdx, &fileData[headerStart + 48], 4);
            std::string materialName = (materialIdx < materials.size()) ? materials[materialIdx] : "Unknown";
            
            // Read vertex and face counts (at +352)
            uint32_t vertexCount, faceCount;
            std::memcpy(&vertexCount, &fileData[headerStart + 352], 4);
            std::memcpy(&faceCount, &fileData[headerStart + 356], 4);
            
            // Read stride marker (at +220)
            uint32_t strideMarker;
            std::memcpy(&strideMarker, &fileData[headerStart + 220], 4);
            uint32_t vertexStride;
            
            switch (strideMarker) {
                case 0x40: vertexStride = 68; break;
                case 0x3C: vertexStride = 64; break;
                case 0x38: vertexStride = 60; break;
                default:
                    std::cout << "Warning: Unknown stride marker 0x" << std::hex << strideMarker 
                              << std::dec << ". Defaulting to 64." << std::endl;
                    vertexStride = 64;
                    break;
            }
            
            SubmeshInfo info;
            info.name = submeshName;
            info.materialName = materialName;
            info.vertexCount = vertexCount;
            info.faceCount = faceCount;
            info.stride = vertexStride;
            submeshes.push_back(info);
            
            std::cout << "  Submesh " << (i + 1) << ": " << submeshName 
                      << " (verts: " << vertexCount << ", faces: " << faceCount 
                      << ", stride: " << vertexStride << ")" << std::endl;
            
            offset += 368;
        }
        
        return true;
    }
    
    std::vector<SubmeshData> extractMeshData() {
        std::vector<SubmeshData> meshData;
        
        // Calculate vertex data offset
        size_t offset = 0;
        offset += 40; // Initial header
        
        uint32_t submeshCount, collisionMeshCount, tagCount, materialsCount;
        std::memcpy(&submeshCount, &fileData[40], 4);
        std::memcpy(&collisionMeshCount, &fileData[44], 4);
        std::memcpy(&tagCount, &fileData[48], 4);
        std::memcpy(&materialsCount, &fileData[52], 4);
        offset += 24;
        
        // Skip materials
        for (uint32_t i = 0; i < materialsCount; i++) {
            size_t nameEnd = offset;
            while (nameEnd < fileData.size() && fileData[nameEnd] != 0) {
                nameEnd++;
            }
            size_t nameLength = nameEnd - offset + 1;
            size_t paddedLength = ((nameLength + 3) / 4) * 4;
            offset += paddedLength;
        }
        
        // Skip tags/collision
        if (tagCount > 0) {
            offset += (tagCount * 48) + 24;
        } else if (collisionMeshCount > 0) {
            offset += collisionMeshCount * 428 + 24;
        } else {
            offset += 24;
        }
        
        // Skip submesh headers
        offset += submeshCount * 368;
        
        // Apply 16-byte alignment
        if (offset % 16 != 0) {
            offset += (16 - (offset % 16));
        }
        
        std::cout << "Vertex data starts at offset: 0x" << std::hex << offset << std::dec << std::endl;
        
        // Extract vertex, normal, UV, and face data for each submesh
        for (const auto& submeshInfo : submeshes) {
            SubmeshData data;
            data.name = submeshInfo.name;
            data.materialName = submeshInfo.materialName;
            
            // Read vertices and normals
            for (uint32_t i = 0; i < submeshInfo.vertexCount; i++) {
                size_t vertOffset = offset + i * submeshInfo.stride;
                
                // Read position (0-11 bytes)
                Vec3 vertex;
                std::memcpy(&vertex.x, &fileData[vertOffset], 4);
                std::memcpy(&vertex.y, &fileData[vertOffset + 4], 4);
                std::memcpy(&vertex.z, &fileData[vertOffset + 8], 4);
                
                // Mirror fix: negate X coordinate
                vertex.x = -vertex.x;
                data.vertices.push_back(vertex);
                
                // Read normal (12-23 bytes)
                Vec3 normal;
                std::memcpy(&normal.x, &fileData[vertOffset + 12], 4);
                std::memcpy(&normal.y, &fileData[vertOffset + 16], 4);
                std::memcpy(&normal.z, &fileData[vertOffset + 20], 4);
                
                // Mirror fix: negate X component
                normal.x = -normal.x;
                data.normals.push_back(normal);
                
                // Read UV (24-27 bytes, half-floats)
                Vec2 uv;
                uv.u = readHalfFloat(fileData.data(), vertOffset + 24);
                uv.v = readHalfFloat(fileData.data(), vertOffset + 26);
                data.uvs.push_back(uv);
            }
            
            offset += submeshInfo.vertexCount * submeshInfo.stride;
            
            // Read faces
            for (uint32_t i = 0; i < submeshInfo.faceCount; i++) {
                Face face;
                std::memcpy(&face.i1, &fileData[offset], 2);
                std::memcpy(&face.i2, &fileData[offset + 2], 2);
                std::memcpy(&face.i3, &fileData[offset + 4], 2);
                
                // Mirror fix: swap winding order
                std::swap(face.i2, face.i3);
                data.faces.push_back(face);
                
                offset += 6;
            }
            
            meshData.push_back(data);
        }
        
        return meshData;
    }
    
    bool exportToOBJ(const std::string& outputPath) {
        std::ofstream objFile(outputPath);
        if (!objFile) {
            std::cerr << "Error: Could not create output file: " << outputPath << std::endl;
            return false;
        }
        
        objFile << "# Exported by SMB2OBJ Converter (C++)\n";
        objFile << std::fixed << std::setprecision(6);
        
        std::vector<SubmeshData> meshData = extractMeshData();
        
        size_t vertexOffset = 0;
        size_t uvOffset = 0;
        size_t normalOffset = 0;
        
        for (size_t i = 0; i < meshData.size(); i++) {
            const auto& submesh = meshData[i];
            
            // Clean name for OBJ
            std::string cleanName = submesh.name;
            std::replace(cleanName.begin(), cleanName.end(), ' ', '_');
            std::string uniqueName = cleanName + "_" + std::to_string(i);
            
            objFile << "o " << uniqueName << "\n";
            
            // Write vertices
            for (const auto& v : submesh.vertices) {
                objFile << "v " << v.x << " " << v.y << " " << v.z << "\n";
            }
            
            // Write UVs (flip V coordinate)
            for (const auto& uv : submesh.uvs) {
                objFile << "vt " << uv.u << " " << (1.0f - uv.v) << "\n";
            }
            
            // Write normals
            for (const auto& n : submesh.normals) {
                objFile << "vn " << n.x << " " << n.y << " " << n.z << "\n";
            }
            
            // Write faces
            for (const auto& f : submesh.faces) {
                size_t v1 = f.i1 + 1 + vertexOffset;
                size_t v2 = f.i2 + 1 + vertexOffset;
                size_t v3 = f.i3 + 1 + vertexOffset;
                
                objFile << "f " << v1 << "/" << v1 << "/" << v1 << " "
                        << v2 << "/" << v2 << "/" << v2 << " "
                        << v3 << "/" << v3 << "/" << v3 << "\n";
            }
            
            vertexOffset += submesh.vertices.size();
            uvOffset += submesh.uvs.size();
            normalOffset += submesh.normals.size();
        }
        
        objFile.close();
        std::cout << "Successfully exported to: " << outputPath << std::endl;
        return true;
    }
};

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: smb2obj input.smb output.obj" << std::endl;
        return 1;
    }
    
    std::string inputPath = argv[1];
    std::string outputPath = argv[2];
    
    std::cout << "SMB to OBJ Converter" << std::endl;
    std::cout << "====================" << std::endl;
    
    SMBConverter converter;
    
    if (!converter.loadFile(inputPath)) {
        return 1;
    }
    
    if (!converter.parseHeader()) {
        return 1;
    }
    
    if (!converter.exportToOBJ(outputPath)) {
        return 1;
    }
    
    std::cout << "\nConversion complete!" << std::endl;
    return 0;
}
