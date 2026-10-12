from texpipe.extract import unsafe_file_names


def test_names_that_survive_as_files_are_safe():
    assert unsafe_file_names(["ch1b1", "ib-b-a.pcx", "hhm3el2 a"]) == set()


def test_a_trailing_space_collides_with_the_plain_name():
    assert unsafe_file_names(["hhm3el2", "hhm3el2 ", "hhm3el2a"]) == {"hhm3el2"}


def test_a_trailing_space_or_dot_alone_is_unsafe():
    assert unsafe_file_names(["wall ", "door."]) == {"wall", "door"}


def test_names_differing_in_case_collide():
    assert unsafe_file_names(["Wall", "wall"]) == {"wall"}
