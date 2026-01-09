# Winter River: Modular Data Center Simulation Platform

A hands-on educational tool that brings AWS data center infrastructure to life through physical hardware simulation.

## Overview

Winter River is a tabletop-scale modular simulation platform designed to teach data center infrastructure concepts through interactive hardware. Built for Seattle University's engineering lab, this system uses ESP32 microcontrollers as "nodes" representing different data center components (servers, PDUs, UPS units, cooling systems, generators) that communicate via MQTT protocol with a Raspberry Pi simulation engine.

## Key Features

- **Modular Architecture**: Plug-and-play components with USB-C connections and custom PCB baseplates
- **Real-Time Visualization**: OLED displays on each node show live status and metrics
- **MQTT Communication**: Industry-standard publish/subscribe messaging for scalable sensor networks
- **Component-Specific Enclosures**: 3D-printed housings with unique shapes for easy identification
- **Power Distribution Simulation**: Models real AWS data center power topology with dual-path redundancy
- **Educational Focus**: Designed for student training and faculty demonstrations

## System Architecture

```
Utility Power → Transformers → Generators/UPS → PDUs → Server Racks
                     ↓
            Raspberry Pi (MQTT Broker)
                     ↓
    ESP32 Nodes (WiFi) ← → Simulation Engine
```

## Components

- **Hardware**: ESP32-WROOM-32 microcontrollers, OLED displays, Raspberry Pi 5, custom PCB baseplates
- **Communication**: MQTT over WiFi
- **Power**: USB-C connections, custom PCB power distribution
- **Enclosures**: 3D-printed PLA housings

## Project Structure

- **Phase 1 (Fall 2025)**: Architecture design, MQTT infrastructure, initial prototyping
- **Phase 2 (Winter 2026)**: Firmware development, PCB design, physical integration
- **Phase 3 (Spring 2026)**: Full system assembly, simulation logic, testing and deployment

## Team

**Seattle University ECE 26.1 Senior Capstone**
- Leilani Gonzalez (CompE)
- Ton Dam Lam/Adam (EE)
- William McDonald (EE)
- Ezekiel Mitchell (CompE)
- Keshav Verma (EE)

**Sponsor**: Amazon Web Services  
**Liaison**: Eric Crompton, Senior Product Design Engineer  
**Advisor**: Dr. Agnieszka Miguel, Seattle University ECE Department

## Documentation

- [Project Proposal](ECE_26_1_Proposal_Draft_2.pdf) - Comprehensive technical proposal
- [AWS Sponsor Brief](AWS_WinterRiver.pdf) - Overview presentation
- [Project Schedule](ECE_26_1_Schedule.pdf) - Development timeline

## Goals

Create an effective hands-on learning tool that makes complex data center infrastructure concepts tangible and interactive, supporting AWS infrastructure training and Seattle University engineering education.

## Status

**Current Phase**: MQTT Infrastructure Development (Winter 2026)  
**Target Deployment**: Spring Quarter 2026

---

*Powered by AWS | Seattle University College of Science and Engineering*
