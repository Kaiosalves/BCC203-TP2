#pragma once

#include <array>

class Item {
  public:
    long registrationNumber;
    float grade;
    std::array<char, 2> state;
    std::array<char, 50> city;
    std::array<char, 30> degree;

    Item() = default;
    Item(long registrationNumber, float grade, std::array<char, 2> state,
         std::array<char, 50> city, std::array<char, 30> degree)
        : registrationNumber(registrationNumber), grade(grade), state(state),
          city(city), degree(degree) {}

    bool operator<(const Item& outro) const {
        if (grade != outro.grade) {
            return grade < outro.grade;
        }
        return registrationNumber < outro.registrationNumber;
    }

    bool operator>(const Item& outro) const {
        if (grade != outro.grade) {
            return grade > outro.grade;
        }
        return registrationNumber > outro.registrationNumber;
    }

    bool operator==(const Item& outro) const {
        return grade == outro.grade && registrationNumber == outro.registrationNumber;
    }
};