// https://openjscad.org/

function mainboard() {
    return union(
        // Сама плата
        cube([80, 60, 1]).translate([0, 0, 5]),
        
        //Место снизу платы
        cube([74, 54, 5]).translate([3, 3, 0]),
        
        // Место сверху и углубление в корпус с левой стороны
        cube([79, 60, 24]).translate([1, 0, 6])
    );
}

function space_after_mainboard() {
    return union(
        // Пластинка на 1 мм выше платы даст корпус заподлицо с платой
        cube([5, 60, 24]),
        
        // Стенка между step_down
        cube([63, 5, 24]).translate([5, 28, 0])
    );
}

function step_down_5v() {
    return union(
        // Сама плата плюс место сверху
        cube([63, 27, 1+24]).translate([0, 0, 5]),
        
        // Место снизу платы в левой части
        cube([20, 27, 5]),
        
        // Место снизу платы в правой части
        cube([15, 27, 5]).translate([63-15, 0, 0])
    );
}

function step_down_3v3() {
    return cube([63, 28, 30]);
}

// Ступенька для крышки
function roof_step() {
    // Для корпуса со стенкой 3мм ступенька будет в полтора мм
    return cube([85+63+3, 63, 1.5]);
}

// Разьём питания
function power_jack() {
    return union(
            circle({r: 3.5}),
            square({size: [7, 8]}).translate([0, -5, 0])
        ).extrude({offset: [0,0,3]})
        .rotateY(270).rotateX(270);
}

// Кнопка включения
function power_button() {
    return cube([3, 8, 18]);
}

// Окно выхода проводов
function wire_window() {
    return union(cube([20, 3, 25]), cube([10, 3, 10]).translate([50, 0, 15]));
}

function main() {
    // Внутренний объём
    const inner_part = union(
        mainboard().translate([3, 3, 3]),
        space_after_mainboard().translate([83, 3, 9]).setColor([0, 50, 50]),
        step_down_5v().translate([88, 60-27+3, 3]),
        step_down_3v3().translate([88, 3, 3]),
        power_jack().translate([153, 28, 30]),
        power_button().translate([1, 5, 17]),
        wire_window().translate([20, 62, 10])
        ,roof_step().translate([1.5, 1.5, 33]).setColor([0, 0, 50])
    );
    
    // Нижняя часть корпуса
    const bottom_box = difference(cube([85+63+6, 63+3, 3+30+1.5]), inner_part);
    
    // Верхняя часть корпуса
    const top_box = difference(cube([85+63+6, 63+3, 3]).translate([0, 0, 3+30]), bottom_box);
    
    //return inner_part;
    //return bottom_box;
    return union(bottom_box, top_box.translate([0, 0, 30]));
}
